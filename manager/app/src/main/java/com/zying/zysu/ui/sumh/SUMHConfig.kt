package com.zying.zysu.ui.sumh

import android.content.Context
import android.content.res.Configuration
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.saveable.listSaver
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.zying.zysu.ui.util.SnackbarController
import com.zying.zysu.ui.util.rememberSnackbarController
import com.zying.zysu.R
import com.zying.zysu.ui.component.YukiIcon
import com.zying.zysu.ui.component.YukiPanel
import com.zying.zysu.ui.component.YukiAlertDialog
import com.zying.zysu.ui.component.YukiTopAppBar
import com.zying.zysu.ui.component.YukiTopBarTitle
import com.zying.zysu.ui.theme.isExpressiveUi
import com.zying.zysu.ui.theme.UtilityPreviewTheme
import com.zying.zysu.ui.sumh.util.SUMHUiAdapter as SUMHManager
import com.zying.zysu.ui.sumh.util.SUMHUiAdapter.SUMHStatus
import com.zying.zysu.ui.theme.getCardColors
import com.zying.zysu.ui.theme.getCardBorder
import com.zying.zysu.ui.theme.getCardElevation
import com.ramcosta.composedestinations.annotation.Destination
import com.ramcosta.composedestinations.annotation.RootGraph
import com.ramcosta.composedestinations.navigation.DestinationsNavigator
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.launch
import ui.screen.moreSettings.component.MoreSettingsItemPosition
import ui.screen.moreSettings.component.SwitchSettingItem


@OptIn(ExperimentalMaterial3Api::class)
@Destination<RootGraph>
@Composable
fun SUMHConfigScreen(navigator: DestinationsNavigator) {
    var selectedSection by rememberSaveable { mutableStateOf(SUMHSection.Status) }
    Scaffold(
        topBar = {
            YukiTopAppBar(
                title = { YukiTopBarTitle(stringResource(R.string.sumh_title)) },
                navigationIcon = {
                    IconButton(onClick = { navigator.popBackStack() }) {
                        YukiIcon(Icons.AutoMirrored.Filled.ArrowBack, stringResource(R.string.back))
                    }
                }
            )
        }
    ) { padding ->
        Column(Modifier.fillMaxSize().padding(padding).consumeWindowInsets(padding)) {
            SUMHTabs(selectedSection) { selectedSection = it }
            Box(Modifier.weight(1f)) { SUMHWorkspace(selectedSection) }
        }
    }
}

@Composable
internal fun StatusTab(
    sumhStatus: SUMHStatus,
    version: String,
    systemInfo: SUMHManager.SystemInfo,
    onRefresh: () -> Unit,
    scrollable: Boolean = true,
    contentPadding: PaddingValues = PaddingValues(horizontal = 18.dp, vertical = 16.dp),
    refreshEnabled: Boolean = true,
    loading: Boolean = false,
    loadError: String? = null,
) {
    Column(
        modifier = (if (scrollable) Modifier.fillMaxSize().verticalScroll(rememberScrollState()) else Modifier.fillMaxWidth())
            .padding(contentPadding),
    ) {
        SUMHStatusSummaryCard(
            modifier = Modifier.fillMaxWidth().height(IntrinsicSize.Min),
            status = sumhStatus,
            version = version,
            systemInfo = systemInfo,
            onRefresh = onRefresh,
            refreshEnabled = refreshEnabled,
            loading = loading,
            loadError = loadError,
            includeRuntimeDetails = true,
            includeKernel = scrollable,
        )
    }
}

@Composable
internal fun SettingSwitch(
    title: String,
    subtitle: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    enabled: Boolean = true,
    icon: ImageVector = Icons.Filled.Settings,
    position: MoreSettingsItemPosition = MoreSettingsItemPosition.Middle,
) {
    SwitchSettingItem(
        icon = icon,
        title = title,
        summary = subtitle,
        checked = checked,
        enabled = enabled,
        groupPosition = position,
        onChange = onCheckedChange,
    )
}

@Composable
internal fun SettingTextField(
    title: String,
    subtitle: String,
    value: String,
    onValueChange: (String) -> Unit,
    onConfirm: () -> Unit,
    enabled: Boolean = true,
    placeholder: String = "",
    position: MoreSettingsItemPosition = MoreSettingsItemPosition.Middle,
) {
    var isEditing by remember { mutableStateOf(false) }
    SUMHControlGroup(position) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = if (enabled) 1f else 0.5f),
                )
                Text(
                    text = subtitle,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = if (enabled) 1f else 0.5f),
                )
            }
            IconButton(onClick = { isEditing = !isEditing }, enabled = enabled) {
                YukiIcon(
                    imageVector = if (isEditing) Icons.Filled.Close else Icons.Filled.Edit,
                    contentDescription = if (isEditing) stringResource(R.string.close) else title,
                )
            }
        }
        if (isEditing) {
            OutlinedTextField(
                value = value,
                onValueChange = onValueChange,
                modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
                label = { Text(title) },
                placeholder = { if (placeholder.isNotEmpty()) Text(placeholder) },
                shape = MaterialTheme.shapes.small,
                singleLine = true,
                enabled = enabled,
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
                keyboardActions = KeyboardActions(onDone = {
                    onConfirm()
                    isEditing = false
                }),
                trailingIcon = {
                    IconButton(onClick = { onConfirm(); isEditing = false }, enabled = enabled) {
                        YukiIcon(Icons.Filled.Done, contentDescription = stringResource(R.string.sumh_rule_save))
                    }
                },
            )
        } else if (value.isNotEmpty()) {
            Text(
                text = value,
                modifier = Modifier.padding(top = 8.dp),
                style = MaterialTheme.typography.bodyMedium,
                fontFamily = FontFamily.Monospace,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

@Composable
internal fun MapsSpoofControls(
    enabled: Boolean,
    onClearRules: () -> Unit,
    onAddRule: (Long, Long, Long, Long, String) -> Unit,
) {
    var showClearConfirm by remember { mutableStateOf(false) }
    var showAddDialog by remember { mutableStateOf(false) }

    if (showClearConfirm) {
        YukiAlertDialog(
            onDismissRequest = { showClearConfirm = false },
            title = { Text(stringResource(R.string.sumh_maps_clear)) },
            text = { Text(stringResource(R.string.sumh_maps_clear_confirm)) },
            confirmButton = {
                TextButton(
                    enabled = enabled,
                    colors = ButtonDefaults.textButtonColors(contentColor = MaterialTheme.colorScheme.error),
                    onClick = {
                        if (!enabled) return@TextButton
                        showClearConfirm = false
                        onClearRules()
                    }
                ) {
                    Text(stringResource(R.string.sumh_rules_clear))
                }
            },
            dismissButton = {
                TextButton(onClick = { showClearConfirm = false }) {
                    Text(stringResource(R.string.sumh_rules_cancel))
                }
            }
        )
    }

    if (showAddDialog) {
        var tIno by remember { mutableStateOf("") }
        var tDev by remember { mutableStateOf("0") }
        var sIno by remember { mutableStateOf("") }
        var sDev by remember { mutableStateOf("0") }
        var path by remember { mutableStateOf("") }
        YukiAlertDialog(
            onDismissRequest = { showAddDialog = false },
            title = { Text(stringResource(R.string.sumh_maps_add_rule)) },
            text = {
                Column(
                    modifier = Modifier.fillMaxWidth().verticalScroll(rememberScrollState()),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    OutlinedTextField(
                        value = tIno,
                        onValueChange = { tIno = it },
                        label = { Text(stringResource(R.string.sumh_maps_target_ino)) },
                        modifier = Modifier.fillMaxWidth(),
                        enabled = enabled,
                        shape = MaterialTheme.shapes.small,
                        singleLine = true
                    )
                    OutlinedTextField(
                        value = tDev,
                        onValueChange = { tDev = it },
                        label = { Text(stringResource(R.string.sumh_maps_target_dev)) },
                        modifier = Modifier.fillMaxWidth(),
                        enabled = enabled,
                        shape = MaterialTheme.shapes.small,
                        singleLine = true
                    )
                    OutlinedTextField(
                        value = sIno,
                        onValueChange = { sIno = it },
                        label = { Text(stringResource(R.string.sumh_maps_spoof_ino)) },
                        modifier = Modifier.fillMaxWidth(),
                        enabled = enabled,
                        shape = MaterialTheme.shapes.small,
                        singleLine = true
                    )
                    OutlinedTextField(
                        value = sDev,
                        onValueChange = { sDev = it },
                        label = { Text(stringResource(R.string.sumh_maps_spoof_dev)) },
                        modifier = Modifier.fillMaxWidth(),
                        enabled = enabled,
                        shape = MaterialTheme.shapes.small,
                        singleLine = true
                    )
                    OutlinedTextField(
                        value = path,
                        onValueChange = { path = it },
                        label = { Text(stringResource(R.string.sumh_maps_spoof_path)) },
                        modifier = Modifier.fillMaxWidth(),
                        enabled = enabled,
                        shape = MaterialTheme.shapes.small,
                        singleLine = true
                    )
                }
            },
            confirmButton = {
                TextButton(
                    enabled = enabled,
                    onClick = {
                        if (!enabled) return@TextButton
                        val ti = tIno.trim().toLongOrNull()
                        val td = tDev.trim().toLongOrNull() ?: 0L
                        val si = sIno.trim().toLongOrNull()
                        val sd = sDev.trim().toLongOrNull() ?: 0L
                        val p = path.trim()
                        if (ti != null && si != null && p.isNotEmpty()) {
                            showAddDialog = false
                            onAddRule(ti, td, si, sd, p)
                        }
                    }
                ) {
                    Text(stringResource(R.string.sumh_maps_add_action))
                }
            },
            dismissButton = {
                TextButton(onClick = { showAddDialog = false }) {
                    Text(stringResource(R.string.sumh_rules_cancel))
                }
            }
        )
    }

    SUMHControlGroup(MoreSettingsItemPosition.Last) {
        Text(
            text = stringResource(R.string.sumh_maps_desc),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Spacer(Modifier.height(8.dp))
        FlowRow(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            OutlinedButton(
                onClick = { if (enabled) showClearConfirm = true },
                enabled = enabled,
                colors = ButtonDefaults.outlinedButtonColors(contentColor = MaterialTheme.colorScheme.error),
                modifier = Modifier.heightIn(min = 48.dp),
                shape = MaterialTheme.shapes.small,
            ) {
                YukiIcon(Icons.Filled.Delete, contentDescription = null)
                Spacer(Modifier.width(8.dp))
                Text(stringResource(R.string.sumh_maps_clear_action))
            }
            FilledTonalButton(
                onClick = { if (enabled) showAddDialog = true },
                enabled = enabled,
                modifier = Modifier.heightIn(min = 48.dp),
                shape = MaterialTheme.shapes.small,
            ) {
                YukiIcon(Icons.Filled.Add, contentDescription = null)
                Spacer(Modifier.width(8.dp))
                Text(stringResource(R.string.sumh_maps_add_action))
            }
        }
    }
}

@Composable
internal fun RulesTab(
    activeRules: List<SUMHManager.ActiveRule>,
    sumhStatus: SUMHStatus,
    controlsEnabled: Boolean,
    onRefresh: () -> Unit,
    onClearAll: () -> Unit
) {
    var showClearDialog by remember { mutableStateOf(false) }

    if (showClearDialog) {
        YukiAlertDialog(
            onDismissRequest = { showClearDialog = false },
            title = { Text(stringResource(R.string.sumh_rules_clear_all)) },
            text = { Text(stringResource(R.string.sumh_rules_clear_confirm)) },
            confirmButton = {
                TextButton(
                    enabled = controlsEnabled,
                    colors = ButtonDefaults.textButtonColors(contentColor = MaterialTheme.colorScheme.error),
                    onClick = {
                        if (!controlsEnabled) return@TextButton
                        showClearDialog = false
                        onClearAll()
                    }
                ) {
                    Text(stringResource(R.string.sumh_rules_clear))
                }
            },
            dismissButton = {
                TextButton(onClick = { showClearDialog = false }) {
                    Text(stringResource(R.string.sumh_rules_cancel))
                }
            }
        )
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp)
    ) {
        if (sumhStatus != SUMHStatus.AVAILABLE) {
            Card(
                modifier = Modifier.fillMaxWidth(),
                shape = sumhCardShape(),
                border = getCardBorder(),
                colors = getCardColors(MaterialTheme.colorScheme.surfaceContainerLow),
                elevation = getCardElevation()
            ) {
                Row(
                    modifier = Modifier.padding(16.dp),
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    YukiIcon(Icons.Filled.Info, contentDescription = null)
                    Text(stringResource(R.string.sumh_rules_not_available))
                }
            }
            return
        }

        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(bottom = 16.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            OutlinedButton(
                onClick = { if (controlsEnabled) onRefresh() },
                enabled = controlsEnabled,
                modifier = Modifier.weight(1f)
            ) {
                YukiIcon(Icons.Filled.Refresh, contentDescription = null)
                Spacer(modifier = Modifier.width(8.dp))
                Text(stringResource(R.string.sumh_rules_refresh))
            }

            OutlinedButton(
                onClick = { if (controlsEnabled) showClearDialog = true },
                enabled = controlsEnabled,
                modifier = Modifier.weight(1f),
                colors = ButtonDefaults.outlinedButtonColors(
                    contentColor = MaterialTheme.colorScheme.error
                )
            ) {
                YukiIcon(Icons.Filled.Delete, contentDescription = null)
                Spacer(modifier = Modifier.width(8.dp))
                Text(stringResource(R.string.sumh_rules_clear_all))
            }
        }

        Text(
            text = pluralStringResource(R.plurals.sumh_rules_count, activeRules.size, activeRules.size),
            style = MaterialTheme.typography.titleSmall,
            modifier = Modifier.padding(bottom = 8.dp)
        )

        LazyColumn(
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            itemsIndexed(activeRules) { index, rule ->
                RuleItem(rule, index, activeRules.size)
            }

            if (activeRules.isEmpty()) {
                item {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(32.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Text(
                            text = stringResource(R.string.sumh_rules_empty),
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun RuleItem(rule: SUMHManager.ActiveRule, index: Int = 0, count: Int = 1) {
    YukiPanel {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(if (isExpressiveUi) 16.dp else 12.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Surface(
                shape = RoundedCornerShape(percent = 50),
                color = if (isExpressiveUi) when (rule.type) {
                    "hide", "mount_hide", "stealth" -> MaterialTheme.colorScheme.tertiaryContainer
                    "add", "inject", "merge" -> MaterialTheme.colorScheme.primaryContainer
                    else -> MaterialTheme.colorScheme.secondaryContainer
                } else when (rule.type) {
                    "add" -> Color(0xFF1B5E20).copy(alpha = 0.3f)
                    "hide" -> Color(0xFFB71C1C).copy(alpha = 0.3f)
                    "inject" -> Color(0xFF1565C0).copy(alpha = 0.3f)
                    "merge" -> Color(0xFF4A148C).copy(alpha = 0.3f)
                    "mount_hide" -> Color(0xFF0D47A1).copy(alpha = 0.3f)
                    "maps_spoof" -> Color(0xFF1A237E).copy(alpha = 0.3f)
                    "statfs_spoof" -> Color(0xFF311B92).copy(alpha = 0.3f)
                    "stealth" -> Color(0xFF37474F).copy(alpha = 0.3f)
                    else -> MaterialTheme.colorScheme.secondaryContainer
                }
            ) {
                Text(
                    text = when (rule.type) {
                        "mount_hide" -> "MOUNT_HIDE"
                        "maps_spoof" -> "MAPS_SPOOF"
                        "statfs_spoof" -> "STATFS_SPOOF"
                        "stealth" -> "STEALTH"
                        else -> rule.type.uppercase()
                    },
                    modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
                    style = MaterialTheme.typography.labelSmall,
                    fontWeight = FontWeight.Bold
                )
            }

            Column(modifier = Modifier.weight(1f)) {
                val displayText = when (rule.type) {
                    "mount_hide" -> stringResource(R.string.sumh_rule_mount_hide)
                    "maps_spoof" -> stringResource(R.string.sumh_rule_maps_spoof)
                    "statfs_spoof" -> stringResource(R.string.sumh_rule_statfs_spoof)
                    "stealth" -> stringResource(R.string.sumh_rule_stealth)
                    else -> rule.src
                }
                ScrollableRuleText(
                    text = displayText,
                    fontFamily = if (rule.type in listOf("mount_hide", "maps_spoof", "statfs_spoof", "stealth"))
                        FontFamily.Default else FontFamily.Monospace,
                )
                if (rule.hideState != null) {
                    Text(userHideStatus(rule), style = MaterialTheme.typography.bodySmall,
                        color = if (rule.hideState == 3) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant)
                }
                if (rule.target != null) {
                    ScrollableRuleText(
                        text = "→ ${rule.target}",
                        fontFamily = FontFamily.Monospace,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                Text(
                    text = stringResource(if (rule.isUserDefined) R.string.sumh_rule_user else R.string.sumh_rule_module),
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}


@Composable
private fun ScrollableRuleText(text: String, fontFamily: FontFamily, color: Color = LocalContentColor.current) {
    val scrollState = rememberScrollState()
    LaunchedEffect(text) { scrollState.scrollTo(0) }
    Text(
        text = text,
        modifier = Modifier.fillMaxWidth().horizontalScroll(scrollState),
        style = MaterialTheme.typography.bodySmall,
        fontFamily = fontFamily,
        color = color,
        maxLines = 1,
        softWrap = false,
        overflow = TextOverflow.Clip,
    )
}

enum class LogLevel(val displayNameRes: Int, val color: Color, val tag: String) {
    VERBOSE(R.string.sumh_logs_filter_verbose, Color(0xFF9C27B0), "VERBOSE"),
    DEBUG(R.string.sumh_logs_filter_debug, Color(0xFF4CAF50), "DEBUG"),
    INFO(R.string.sumh_logs_filter_info, Color(0xFF2196F3), "INFO"),
    WARN(R.string.sumh_logs_filter_warn, Color(0xFFFF9800), "WARN"),
    ERROR(R.string.sumh_logs_filter_error, Color(0xFFF44336), "ERROR")
}

private val taggedLogLevel = Regex("\\[(VERBOSE|DEBUG|INFO|WARN(?:ING)?|ERROR)]", RegexOption.IGNORE_CASE)

private fun logLevelOf(line: String): LogLevel? {
    val tag = taggedLogLevel.find(line)?.groupValues?.get(1)?.uppercase()
    if (tag != null) return LogLevel.entries.firstOrNull { it.tag == if (tag == "WARNING") "WARN" else tag }
    return LogLevel.entries.asReversed().firstOrNull { line.contains(it.tag, ignoreCase = true) }
}

@Composable
internal fun LogsTab(
    showKernelLog: Boolean,
    onToggleLogType: () -> Unit,
    logContent: String,
    onRefreshLog: () -> Unit,
    onClearLog: () -> Unit,
) {
    val context = LocalContext.current
    val resources = LocalResources.current
    val snackbarHostState = rememberSnackbarController()
    val coroutineScope = rememberCoroutineScope()

    var selectedLogLevels by rememberSaveable(
        stateSaver = listSaver(
            save = { levels: Set<LogLevel> -> levels.map { it.name } },
            restore = { names -> names.map(LogLevel::valueOf).toSet() },
        )
    ) { mutableStateOf(emptySet<LogLevel>()) }
    var filterExpanded by remember { mutableStateOf(false) }
    var sourceExpanded by remember { mutableStateOf(false) }
    var searchExpanded by rememberSaveable { mutableStateOf(false) }
    var search by rememberSaveable { mutableStateOf("") }
    var clearConfirm by remember { mutableStateOf(false) }
    val searchFocus = remember { FocusRequester() }
    val focusManager = LocalFocusManager.current
    val keyboard = LocalSoftwareKeyboardController.current

    fun closeSearch() {
        search = ""
        searchExpanded = false
        focusManager.clearFocus()
        keyboard?.hide()
    }

    BackHandler(enabled = searchExpanded) { closeSearch() }
    LaunchedEffect(searchExpanded) {
        if (searchExpanded) searchFocus.requestFocus()
    }

    val filteredLogContent = remember(logContent, selectedLogLevels, search) {
        logContent.lineSequence().filter { line ->
            (search.isEmpty() || line.contains(search, ignoreCase = true)) &&
                (selectedLogLevels.isEmpty() || logLevelOf(line) in selectedLogLevels)
        }.joinToString("\n")
    }

    if (clearConfirm) YukiAlertDialog(
        onDismissRequest = { clearConfirm = false },
        title = { Text(stringResource(R.string.sumh_logs_clear)) },
        text = { Text(stringResource(R.string.sumh_logs_clear_confirm)) },
        confirmButton = { TextButton(onClick = { clearConfirm = false; onClearLog() }) { Text(stringResource(android.R.string.ok)) } },
        dismissButton = { TextButton(onClick = { clearConfirm = false }) { Text(stringResource(android.R.string.cancel)) } },
    )

    val annotatedLogContent = remember(filteredLogContent) {
        buildAnnotatedString {
            filteredLogContent.lines().forEach { line ->
                val color = logLevelOf(line)?.color ?: Color.White
                withStyle(style = SpanStyle(color = color)) {
                    append(line)
                }
                append("\n")
            }
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp)
    ) {

        SUMHLogActions {
            if (searchExpanded) {
                OutlinedTextField(
                    value = search,
                    onValueChange = { search = it },
                    modifier = Modifier.weight(1f).focusRequester(searchFocus),
                    singleLine = true,
                    placeholder = {
                        Text(stringResource(R.string.sumh_logs_search), maxLines = 1, overflow = TextOverflow.Ellipsis)
                    },
                    trailingIcon = {
                        IconButton(onClick = { closeSearch() }) {
                            YukiIcon(Icons.Filled.Close, stringResource(R.string.close))
                        }
                    },
                    keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
                    keyboardActions = KeyboardActions(onSearch = {
                        focusManager.clearFocus()
                        keyboard?.hide()
                    }),
                )
            } else {
                Box(Modifier.weight(1f)) {
                    FilledTonalButton(onClick = { sourceExpanded = true }, modifier = Modifier.fillMaxWidth()) {
                        Text(
                            stringResource(if (showKernelLog) R.string.sumh_logs_kernel else R.string.sumh_logs_daemon),
                            modifier = Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis,
                        )
                        YukiIcon(Icons.Filled.ArrowDropDown, null, Modifier.size(18.dp))
                    }
                    DropdownMenu(expanded = sourceExpanded, onDismissRequest = { sourceExpanded = false }) {
                        listOf(false, true).forEach { kernel ->
                            DropdownMenuItem(
                                text = { Text(stringResource(if (kernel) R.string.sumh_logs_kernel else R.string.sumh_logs_daemon)) },
                                trailingIcon = {
                                    if (showKernelLog == kernel) YukiIcon(Icons.Filled.Check, null)
                                },
                                onClick = {
                                    sourceExpanded = false
                                    if (showKernelLog != kernel) onToggleLogType()
                                },
                            )
                        }
                    }
                }
                IconButton(onClick = { searchExpanded = true }) {
                    YukiIcon(Icons.Filled.Search, stringResource(R.string.sumh_logs_search))
                }
            }
            IconButton(onClick = onRefreshLog) {
                YukiIcon(Icons.Filled.Refresh, stringResource(R.string.sumh_rules_refresh))
            }

            Box {
                IconButton(onClick = { filterExpanded = true }) {
                    YukiIcon(
                        Icons.Filled.MoreVert,
                        contentDescription = stringResource(R.string.sumh_logs_actions),
                        tint = if (selectedLogLevels.isEmpty()) LocalContentColor.current else MaterialTheme.colorScheme.primary,
                    )
                }
                DropdownMenu(
                    expanded = filterExpanded,
                    onDismissRequest = { filterExpanded = false },
                    modifier = Modifier.widthIn(min = 180.dp)
                ) {
                    DropdownMenuItem(
                        text = { Text(stringResource(R.string.sumh_logs_copy)) },
                        leadingIcon = { YukiIcon(Icons.Filled.ContentCopy, null) },
                        onClick = {
                            filterExpanded = false
                            val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as android.content.ClipboardManager
                            val clip = android.content.ClipData.newPlainText("SUMH Log", filteredLogContent)
                            clipboard.setPrimaryClip(clip)
                            coroutineScope.launchUi(snackbarHostState) {
                                snackbarHostState.showSnackbar(resources.getString(R.string.sumh_logs_copy_success))
                            }
                        },
                    )
                    DropdownMenuItem(
                        text = { Text(stringResource(R.string.sumh_logs_clear)) },
                        leadingIcon = { YukiIcon(Icons.Filled.Delete, null) },
                        enabled = !showKernelLog,
                        onClick = { filterExpanded = false; clearConfirm = true },
                    )
                    HorizontalDivider()
                    Column(
                        modifier = Modifier.padding(vertical = 8.dp),
                        verticalArrangement = Arrangement.spacedBy(4.dp)
                    ) {
                        Text(
                            stringResource(R.string.sumh_logs_filter),
                            style = MaterialTheme.typography.labelLarge,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.padding(horizontal = 12.dp),
                        )
                        FilterChip(
                            selected = selectedLogLevels.isEmpty(),
                            onClick = {
                                selectedLogLevels = emptySet()
                                filterExpanded = false
                            },
                            label = { Text(stringResource(R.string.sumh_logs_filter_all)) },
                            modifier = Modifier.padding(horizontal = 12.dp)
                        )
                        HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))
                        FlowRow(
                            modifier = Modifier.padding(horizontal = 12.dp),
                            horizontalArrangement = Arrangement.spacedBy(6.dp),
                            verticalArrangement = Arrangement.spacedBy(6.dp)
                        ) {
                            LogLevel.entries.forEach { level ->
                                FilterChip(
                                    selected = level in selectedLogLevels,
                                    onClick = {
                                        selectedLogLevels = if (level in selectedLogLevels) {
                                            selectedLogLevels - level
                                        } else {
                                            selectedLogLevels + level
                                        }
                                    },
                                    label = { Text(stringResource(level.displayNameRes)) },
                                    leadingIcon = {
                                        Box(
                                            modifier = Modifier
                                                .size(8.dp)
                                                .background(level.color, shape = RoundedCornerShape(4.dp))
                                        )
                                    }
                                )
                            }
                        }
                    }
                }
            }
        }

        Card(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f),
            shape = sumhCardShape(),
            border = getCardBorder(),
            colors = CardDefaults.cardColors(
                containerColor = Color(0xFF1E1E1E)
            )
        ) {
            Box(modifier = Modifier.fillMaxSize()) {
                val scrollState = rememberScrollState()

                Text(
                    text = annotatedLogContent,
                    modifier = Modifier
                        .fillMaxSize()
                        .verticalScroll(scrollState)
                        .padding(12.dp),
                    style = MaterialTheme.typography.bodySmall,
                    fontFamily = FontFamily.Monospace,
                    fontSize = if (isExpressiveUi) 12.sp else 11.sp
                )

                SnackbarHost(
                    hostState = snackbarHostState.hostState,
                    modifier = Modifier
                        .align(Alignment.BottomCenter)
                        .padding(16.dp)
                )
            }
        }
    }
}

internal fun CoroutineScope.launchUi(
    snackbar: SnackbarController,
    start: CoroutineStart = CoroutineStart.DEFAULT,
    action: suspend () -> Unit,
) = launch(start = start) {
    try { action() }
    catch (error: Exception) {
        if (error is CancellationException) throw error
        snackbar.showSnackbar(error.message ?: "Operation failed")
    }
}

@Preview(name = "SUMH status - light", widthDp = 360, heightDp = 800, locale = "zh-rCN")
@Preview(name = "SUMH status - dark", widthDp = 360, heightDp = 800, locale = "zh-rCN", uiMode = Configuration.UI_MODE_NIGHT_YES)
@Preview(name = "SUMH status - large text", widthDp = 320, heightDp = 800, locale = "zh-rCN", fontScale = 2f)
@Preview(name = "SUMH status - landscape", widthDp = 800, heightDp = 360)
@Composable
private fun SUMHStatusPreview() {
    UtilityPreviewTheme {
        Surface(color = MaterialTheme.colorScheme.background) {
            StatusTab(
                sumhStatus = SUMHStatus.AVAILABLE,
                version = "1",
                systemInfo = SUMHManager.SystemInfo(
                    kernel = "6.12.52-android16-5-g2cf44e703c2c-ab14552068-4k",
                    mountBase = "none",
                    activeMounts = emptyList(),
                    sumhModuleIds = emptyList(),
                    sumhMismatch = false,
                    mismatchMessage = null,
                    hooks = "vfs_getattr\nshow_vfsmnt",
                    viewsEnabled = true,
                ),
                onRefresh = {},
            )
        }
    }
}
