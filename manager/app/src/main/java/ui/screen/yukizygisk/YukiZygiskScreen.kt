package ui.screen.yukizygisk

import android.util.Log
import android.content.res.Configuration
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.Article
import androidx.compose.material.icons.automirrored.outlined.HelpOutline
import androidx.compose.material.icons.filled.Adb
import androidx.compose.material.icons.filled.Bolt
import androidx.compose.material.icons.filled.Extension
import androidx.compose.material.icons.filled.SwapHoriz
import androidx.compose.material.icons.filled.Terminal
import androidx.compose.material.icons.outlined.Cancel
import androidx.compose.material.icons.outlined.Link
import androidx.compose.material.icons.outlined.TaskAlt
import androidx.compose.material.icons.outlined.VisibilityOff
import androidx.compose.material.icons.outlined.Warning
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.LinearProgressIndicator
import com.anatdx.yukisu.ui.screen.WorkspaceOperationGuard
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.rememberTopAppBarState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import com.ramcosta.composedestinations.annotation.Destination
import com.ramcosta.composedestinations.annotation.RootGraph
import com.ramcosta.composedestinations.navigation.DestinationsNavigator
import com.anatdx.yukisu.R
import com.anatdx.yukisu.Natives
import com.anatdx.yukisu.ui.component.KsuIsValid
import com.anatdx.yukisu.ui.component.YukiIcon
import com.anatdx.yukisu.ui.component.YukiTopAppBar
import com.anatdx.yukisu.ui.component.YukiTopBarTitle
import com.anatdx.yukisu.ui.component.YukiAlertDialog
import com.anatdx.yukisu.ui.theme.CardConfig
import com.anatdx.yukisu.ui.theme.UtilityPreviewTheme
import com.anatdx.yukisu.ui.util.rememberSnackbarController
import com.anatdx.yukisu.ui.util.LocalSnackbarHost
import com.anatdx.yukisu.ui.util.execKsud
import com.anatdx.yukisu.ui.util.getYukiZygiskStatusJson
import com.anatdx.yukisu.ui.util.ksudReadString
import com.ramcosta.composedestinations.generated.destinations.YukiZygiskScreenDestination
import com.topjohnwu.superuser.io.SuFile
import com.topjohnwu.superuser.io.SuFileInputStream
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONObject
import ui.screen.moreSettings.component.MoreSettingsItemPosition
import ui.screen.moreSettings.component.SettingsCard
import ui.screen.moreSettings.component.SwitchSettingItem
import ui.screen.feature.YukiZygiskFeatureSwitch
import ui.screen.feature.rememberFeatureToggleState

private const val TAG = "YukiZygiskScreen"
private val yzConfigWriteMutex = Mutex()

data class YzConfig(
    val yukilinker: Boolean = true,
    val anonymousMemory: Boolean = true,
    val earlyLoad: Boolean = false,
    val denylistMode: Int = 0,
    val dmesgLog: Boolean = false,
    val crashProtection: Boolean = false,
)

private suspend fun readYzConfig(): YzConfig = withContext(Dispatchers.IO) {
    val raw = runCatching {
        ksudReadString("yzctl config get")
    }.getOrNull()
    if (raw.isNullOrBlank()) return@withContext YzConfig()
    try {
        val o = JSONObject(raw)
        YzConfig(
            yukilinker = o.optBoolean("yukilinker", true),
            anonymousMemory = o.optBoolean("anonymous_memory", true),
            earlyLoad = o.optBoolean("early_load", false),
            denylistMode = o.optInt("denylist_mode", 0),
            dmesgLog = o.optBoolean("dmesg_log", false),
            crashProtection = o.optBoolean("crash_protection", false),
        )
    } catch (_: Exception) {
        YzConfig()
    }
}

private suspend fun writeYzConfig(cfg: YzConfig): Boolean = yzConfigWriteMutex.withLock {
    withContext(Dispatchers.IO) {
        runCatching {
            check(execKsud(
                "yzctl config set yukilinker ${cfg.yukilinker}" +
                    " anonymous_memory ${cfg.anonymousMemory} early_load ${cfg.earlyLoad}" +
                    " denylist_mode ${cfg.denylistMode} dmesg_log ${cfg.dmesgLog}" +
                    " crash_protection ${cfg.crashProtection}"
            ))
        }.onFailure { Log.e(TAG, "Failed to apply YukiZygisk configuration", it) }.isSuccess
    }
}

private enum class MonitorState {
    Injected,
    Unsupported32,
    Crashed,
    Failed,
    Unknown,
}

private enum class NativeMonitorMode {
    Module,
    Process,
}

private data class MonitorDialogState(
    val title: String,
    val message: String,
)

private fun parseMonitorState(value: String): MonitorState = when (value) {
    "injected" -> MonitorState.Injected
    "unsupported32" -> MonitorState.Unsupported32
    "crashed" -> MonitorState.Crashed
    "safemode" -> MonitorState.Crashed
    "failed" -> MonitorState.Failed
    else -> MonitorState.Unknown
}

private data class ZygoteMonitorEntry(
    val pid: Int,
    val name: String,
    val abi: String,
    val state: MonitorState,
    val generation: Int = 0,
    val modules: List<String> = emptyList(),
    val moduleMonitorAvailable: Boolean = false,
)

private data class NativeModuleEntry(
    val name: String,
    val id: String,
    val targetType: String,
    val target: String,
    val state: MonitorState,
)

private data class NativeInjection(
    val pid: Int,
    val process: String,
    val module: String,
    val targetType: String,
    val target: String,
    val abi: String,
    val state: MonitorState,
)

private data class NativeProcessEntry(
    val pid: Int,
    val process: String,
    val abi: String,
    val modules: List<String>,
    val state: MonitorState,
)

private data class NativeModuleMonitorEntry(
    val name: String,
    val id: String,
    val scopes: List<NativeModuleScope>,
    val state: MonitorState,
    val crashEvidence: List<CrashEvidence>,
)

private data class NativeModuleScope(
    val targetType: String,
    val target: String,
    val state: MonitorState,
    val targets: List<NativeModuleTarget>,
)

private data class NativeModuleTarget(
    val process: String,
    val pid: Int,
)

private data class CrashEvidence(
    val module: String,
    val process: String,
    val tombstone: String,
    val frame: String,
    val zygotePid: Int,
    val generation: Int,
    val pid: Int,
    val abi: String,
    val timestamp: String,
)

private fun nativeProcessDisplayName(value: String): String =
    value.substringAfterLast('/').ifBlank { value }

private fun aggregateMonitorState(states: List<MonitorState>): MonitorState = when {
    MonitorState.Crashed in states -> MonitorState.Crashed
    MonitorState.Failed in states -> MonitorState.Failed
    MonitorState.Injected in states -> MonitorState.Injected
    MonitorState.Unsupported32 in states -> MonitorState.Unsupported32
    else -> MonitorState.Unknown
}

private fun buildNativeModuleRows(
    modules: List<NativeModuleEntry>,
    injections: List<NativeInjection>,
    crashEvidence: List<CrashEvidence>,
): List<NativeModuleMonitorEntry> {
    val injectionsByModule = injections.groupBy { it.module }
    return modules.groupBy { it.id }.map { (id, entries) ->
        val scopes = entries.groupBy { it.targetType to it.target }.map { (_, scopeEntries) ->
            val entry = scopeEntries.first()
            val targets = injectionsByModule[id].orEmpty()
                .filter {
                    it.targetType == entry.targetType && it.target == entry.target && it.pid > 0
                }
                .map {
                    NativeModuleTarget(
                        process = nativeProcessDisplayName(it.process.ifBlank { it.target }),
                        pid = it.pid,
                    )
                }
                .distinctBy { it.pid }
                .sortedWith(compareBy<NativeModuleTarget> { it.process }.thenBy { it.pid })
            NativeModuleScope(
                targetType = entry.targetType,
                target = entry.target,
                state = aggregateMonitorState(scopeEntries.map { it.state }),
                targets = targets,
            )
        }
        NativeModuleMonitorEntry(
            name = entries.first().name,
            id = id,
            scopes = scopes,
            state = aggregateMonitorState(scopes.map { it.state }),
            crashEvidence = crashEvidence.filter { it.module == id },
        )
    }
}

private data class YzStatus(
    val zygotes: List<ZygoteMonitorEntry>,
    val modules: List<String>,
    val runtime: List<RuntimeMonitorEntry>,
    val nativeModules: List<NativeModuleEntry>,
    val nativeInjections: List<NativeInjection>,
    val crashEvidence: List<CrashEvidence>,
    val suspendedModules: Set<String>,
)

private data class ModuleDisplayEntry(
    val name: String,
    val id: String,
    val abis: List<String>,
    val state: MonitorState,
    val crashEvidence: List<CrashEvidence> = emptyList(),
    val suspended: Boolean = false,
)

private data class RuntimeMonitorEntry(
    val pid: Int,
    val generation: Int,
    val kind: String,
    val abi: String,
    val module: String,
    val state: MonitorState,
)

private fun zygoteModules(
    runtime: List<RuntimeMonitorEntry>,
    pid: Int,
    generation: Int,
    abi: String,
): List<String> = runtime.filter {
    it.kind == "zygote" && it.pid == pid && it.generation == generation &&
        pid > 0 && generation != 0 && it.abi == abi &&
        it.module.isNotBlank() && it.state == MonitorState.Injected
}.map { it.module }.distinct()

private fun parseYzStatus(json: String): YzStatus? = runCatching {
    val o = JSONObject(json)
    val runtime = o.optJSONArray("runtime")?.let { a ->
        (0 until a.length()).map { i ->
            val r = a.getJSONObject(i)
            RuntimeMonitorEntry(
                pid = r.optInt("pid", 0),
                generation = r.optInt("generation", 0),
                kind = r.optString("kind", ""),
                abi = r.optString("abi", "unknown"),
                module = r.optString("module", ""),
                state = parseMonitorState(r.optString("state", "unknown")),
            )
        }
    } ?: emptyList()
    val modules = o.optJSONArray("modules")?.let { a ->
        (0 until a.length()).map { a.getString(it) }
    } ?: emptyList()
    val legacyZygotes = o.optJSONArray("zygotes")?.let { a ->
        (0 until a.length()).map { i ->
            val z = a.getJSONObject(i)
            ZygoteMonitorEntry(
                pid = z.optInt("pid", 0),
                name = z.optString("target").ifBlank { z.optString("name", "zygote") },
                abi = z.optString("abi", "unknown"),
                state = MonitorState.Injected,
                generation = z.optInt("generation", 0),
                modules = zygoteModules(
                    runtime, z.optInt("pid", 0), z.optInt("generation", 0), z.optString("abi"),
                ),
                moduleMonitorAvailable = o.optBoolean("zygisk_module_monitor", false),
            )
        }
    } ?: emptyList()
    val monitoredZygoteArray = o.optJSONArray("zygote_monitor")
    val monitoredZygotes = monitoredZygoteArray?.let { a ->
        (0 until a.length()).map { i ->
            val z = a.getJSONObject(i)
            ZygoteMonitorEntry(
                pid = z.optInt("pid", 0),
                name = z.optString("target").ifBlank { z.optString("name", "zygote") },
                abi = z.optString("abi", "unknown"),
                state = parseMonitorState(z.optString("state", "unknown")),
                generation = z.optInt("generation", 0),
                modules = zygoteModules(
                    runtime, z.optInt("pid", 0), z.optInt("generation", 0), z.optString("abi"),
                ),
                moduleMonitorAvailable = o.optBoolean("zygisk_module_monitor", false),
            )
        }
    } ?: emptyList()
    val zygotes = if (monitoredZygoteArray != null) monitoredZygotes else legacyZygotes
    val nativeModules = o.optJSONArray("native_modules")?.let { a ->
        (0 until a.length()).map { i ->
            val n = a.getJSONObject(i)
            NativeModuleEntry(
                name = n.optString("name", ""),
                id = n.optString("id", ""),
                targetType = n.optString("target_type", "name"),
                target = n.optString("target", ""),
                state = parseMonitorState(n.optString("state", "unknown")),
            )
        }
    } ?: emptyList()
    val nativeInjections = o.optJSONArray("native_injections")?.let { a ->
        (0 until a.length()).map { i ->
            val n = a.getJSONObject(i)
            NativeInjection(
                pid = n.optInt("pid", 0),
                process = n.optString("process", ""),
                module = n.optString("module", ""),
                targetType = n.optString("target_type", "name"),
                target = n.optString("target", ""),
                abi = n.optString("abi", "unknown"),
                state = parseMonitorState(n.optString("state", "unknown")),
            )
        }
    } ?: emptyList()
    val crashEvidence = o.optJSONArray("crash_evidence")?.let { a ->
        (0 until a.length()).mapNotNull { i ->
            val e = a.optJSONObject(i) ?: return@mapNotNull null
            CrashEvidence(
                module = e.optString("module", ""),
                process = e.optString("process", ""),
                tombstone = e.optString("tombstone", ""),
                frame = e.optString("frame", ""),
                zygotePid = e.optInt("zygote_pid", 0),
                generation = e.optInt("generation", 0),
                pid = e.optInt("pid", 0),
                abi = e.optString("abi", ""),
                timestamp = e.optString("timestamp", ""),
            )
        }
    } ?: emptyList()
    YzStatus(
        zygotes,
        modules,
        runtime,
        nativeModules,
        nativeInjections,
        crashEvidence,
        o.optJSONArray("suspended_modules")?.let { a ->
            (0 until a.length()).map { a.getString(it) }.toSet()
        } ?: emptySet(),
    )
}.getOrNull()

private data class YzSnapshot(
    val zygotes: List<ZygoteMonitorEntry>,
    val modules: List<ModuleDisplayEntry>,
    val nativeModules: List<NativeModuleEntry>,
    val nativeInjections: List<NativeInjection>,
    val crashEvidence: List<CrashEvidence>,
)

private const val YZ_POLL_INTERVAL_MS = 2000L

private val YZ_MODULE_ABIS = listOf("arm64-v8a", "armeabi-v7a")

private fun readModuleDisplayName(moduleId: String): String {
    if (moduleId.isBlank()) return moduleId
    return runCatching {
        val propFile = SuFile("/data/adb/modules/$moduleId/module.prop")
        if (!propFile.isFile) return@runCatching moduleId
        SuFileInputStream.open(propFile).bufferedReader().useLines { lines ->
            lines.firstNotNullOfOrNull { line ->
                line.takeIf { it.startsWith("name=") }
                    ?.substringAfter('=')
                    ?.trim()
                    ?.takeIf { it.isNotEmpty() }
            } ?: moduleId
        }
    }.getOrDefault(moduleId)
}

private fun readModuleDisplayNames(moduleIds: Collection<String>): Map<String, String> =
    moduleIds.distinct().associateWith(::readModuleDisplayName)

private fun readModuleArchitectures(moduleId: String): List<String> =
    YZ_MODULE_ABIS.filter { abi ->
        SuFile("/data/adb/modules/$moduleId/zygisk/$abi.so").isFile
    }

private fun readModuleArchitectures(moduleIds: Collection<String>): Map<String, List<String>> =
    moduleIds.distinct().associateWith(::readModuleArchitectures)

private fun zygiskModuleState(
    moduleId: String,
    abis: List<String>,
    zygotes: List<ZygoteMonitorEntry>,
    runtime: List<RuntimeMonitorEntry>,
): MonitorState {
    var injected = false
    var failed = false
    for (abi in abis.filter { it in YZ_MODULE_ABIS }) {
        val matchingZygotes = zygotes.filter { it.abi == abi && it.pid > 0 }
        if (matchingZygotes.isEmpty()) continue
        if (matchingZygotes.any { it.state == MonitorState.Crashed || it.state == MonitorState.Failed }) {
            failed = true
            continue
        }
        val records = runtime.filter {
            it.kind == "zygote" && it.module == moduleId && it.abi == abi &&
                matchingZygotes.any { z ->
                    z.moduleMonitorAvailable && z.generation != 0 &&
                        z.pid == it.pid && z.generation == it.generation
                }
        }
        if (records.any { it.state == MonitorState.Crashed || it.state == MonitorState.Failed }) {
            failed = true
        } else if (records.any { it.state == MonitorState.Injected }) {
            injected = true
        }
    }
    return when {
        failed -> MonitorState.Failed
        injected -> MonitorState.Injected
        else -> MonitorState.Unknown
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Destination<RootGraph>
@Composable
fun YukiZygiskScreen(navigator: DestinationsNavigator) {
    val topAppBarState = rememberTopAppBarState()
    val scrollBehavior = TopAppBarDefaults.pinnedScrollBehavior(topAppBarState)
    var saving by remember { mutableStateOf(false) }
    val snackbar = rememberSnackbarController()
    WorkspaceOperationGuard(saving, YukiZygiskScreenDestination.route)
    Scaffold(
        modifier = Modifier.nestedScroll(scrollBehavior.nestedScrollConnection),
        topBar = {
            YukiZygiskTopBar(
                onBack = { if (!saving) navigator.popBackStack() },
                backEnabled = !saving,
                scrollBehavior = scrollBehavior,
            )
        },
        snackbarHost = { SnackbarHost(snackbar.hostState) },
        contentWindowInsets = WindowInsets.safeDrawing,
    ) { padding ->
        CompositionLocalProvider(LocalSnackbarHost provides snackbar) {
            Box(Modifier.fillMaxSize().padding(padding).consumeWindowInsets(padding)) {
                KsuIsValid {
                    YukiZygiskContent(onSavingChanged = { saving = it })
                }
            }
        }
    }
}

@Composable
private fun YukiZygiskContent(onSavingChanged: (Boolean) -> Unit) {
    val feature = rememberFeatureToggleState(Natives.FEATURE_YUKIZYGISK)
    var configurationSaving by remember { mutableStateOf(false) }
    val savingCallback by rememberUpdatedState(onSavingChanged)
    SideEffect { savingCallback(feature.saving || configurationSaving) }
    DisposableEffect(Unit) { onDispose { savingCallback(false) } }

    YukiZygiskPageLayout(
        enabled = feature.checked,
        switching = feature.saving,
        masterSwitch = {
            YukiZygiskFeatureSwitch(feature, enabled = !configurationSaving)
        },
    ) {
        InjectionWorkspace(onSavingChanged = { configurationSaving = it })
    }
}

/** One scroll region; disabled or pending enablement never composes runtime content. */
@Composable
private fun YukiZygiskPageLayout(
    enabled: Boolean,
    switching: Boolean,
    masterSwitch: @Composable () -> Unit,
    content: @Composable () -> Unit,
) {
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp, vertical = 8.dp),
    ) {
        Surface(
            modifier = Modifier.fillMaxWidth().padding(bottom = 8.dp),
            shape = MaterialTheme.shapes.medium,
            color = MaterialTheme.colorScheme.surfaceContainerLow.copy(alpha = CardConfig.cardAlpha),
        ) {
            masterSwitch()
        }
        if (switching) {
            LinearProgressIndicator(Modifier.fillMaxWidth().height(2.dp))
        }
        if (enabled && !switching) content()
    }
}

/** Enabled-only content shares its host's scroll, insets and snackbar. */
@Composable
private fun InjectionWorkspace(
    onSavingChanged: (Boolean) -> Unit = {},
) {
    val scope = rememberCoroutineScope()
    val snackBarHost = LocalSnackbarHost.current

    var config by remember { mutableStateOf(YzConfig()) }
    var configLoaded by remember { mutableStateOf(false) }
    var saving by remember { mutableStateOf(false) }
    val savingCallback by rememberUpdatedState(onSavingChanged)
    SideEffect { savingCallback(saving) }
    DisposableEffect(Unit) { onDispose { savingCallback(false) } }
    var monitoredZygotes by remember { mutableStateOf<List<ZygoteMonitorEntry>>(emptyList()) }
    var zygiskModules by remember { mutableStateOf<List<ModuleDisplayEntry>>(emptyList()) }
    var nativeModules by remember { mutableStateOf<List<NativeModuleEntry>>(emptyList()) }
    var nativeInjections by remember { mutableStateOf<List<NativeInjection>>(emptyList()) }
    var crashEvidence by remember { mutableStateOf<List<CrashEvidence>>(emptyList()) }
    var nativeMonitorMode by remember { mutableStateOf(NativeMonitorMode.Module) }
    var monitorDialog by remember { mutableStateOf<MonitorDialogState?>(null) }

    LaunchedEffect(Unit) {
        config = readYzConfig()
        configLoaded = true
    }

    LaunchedEffect(Unit) {
        val moduleNameCache = mutableMapOf<String, String>()
        val moduleAbiCache = mutableMapOf<String, List<String>>()
        while (true) {
            val snapshot = withContext(Dispatchers.IO) {
                val st = getYukiZygiskStatusJson()?.let(::parseYzStatus)
                    ?: return@withContext null
                val moduleIds = st.modules + st.nativeModules.map { it.id } +
                    st.zygotes.flatMap { it.modules }
                val missingModuleIds = moduleIds.distinct().filterNot(moduleNameCache::containsKey)
                if (missingModuleIds.isNotEmpty()) {
                    moduleNameCache.putAll(readModuleDisplayNames(missingModuleIds))
                    moduleAbiCache.putAll(readModuleArchitectures(missingModuleIds))
                }
                val moduleEntries = st.modules.map { id ->
                    ModuleDisplayEntry(
                        name = moduleNameCache[id] ?: id,
                        id = id,
                        abis = moduleAbiCache[id].orEmpty(),
                        state = zygiskModuleState(
                            id, moduleAbiCache[id].orEmpty(), st.zygotes, st.runtime,
                        ),
                        crashEvidence = st.crashEvidence.filter { it.module == id },
                        suspended = id in st.suspendedModules,
                    )
                }
                val zygotes = st.zygotes.map { zygote ->
                    zygote.copy(
                        modules = zygote.modules.map { moduleNameCache[it] ?: it },
                    )
                }
                YzSnapshot(
                    zygotes = zygotes,
                    modules = moduleEntries,
                    nativeModules = st.nativeModules.map { module ->
                        module.copy(name = moduleNameCache[module.id] ?: module.id)
                    },
                    nativeInjections = st.nativeInjections,
                    crashEvidence = st.crashEvidence,
                )
            }
            if (snapshot != null) {
                monitoredZygotes = snapshot.zygotes
                zygiskModules = snapshot.modules
                nativeModules = snapshot.nativeModules
                nativeInjections = snapshot.nativeInjections
                crashEvidence = snapshot.crashEvidence
            } else {
                monitoredZygotes = emptyList()
                zygiskModules = emptyList()
                nativeModules = emptyList()
                nativeInjections = emptyList()
                crashEvidence = emptyList()
            }
            delay(YZ_POLL_INTERVAL_MS)
        }
    }

    val saveFailedMessage = stringResource(R.string.yukizygisk_config_save_failed)
    fun save(newCfg: YzConfig) {
        if (!configLoaded || saving) return
        saving = true
        config = newCfg
        // Enter the protected write before a host disposal can cancel a queued launch.
        scope.launch(start = CoroutineStart.UNDISPATCHED) {
            val saved = withContext(NonCancellable) {
                try {
                    val success = writeYzConfig(newCfg)
                    if (!success) config = readYzConfig()
                    success
                } finally {
                    saving = false
                }
            }
            if (!saved) snackBarHost.showSnackbar(saveFailedMessage)
        }
    }

    monitorDialog?.let { dialog ->
        YukiAlertDialog(
            onDismissRequest = { monitorDialog = null },
            title = { Text(dialog.title) },
            text = { Text(dialog.message, modifier = Modifier.verticalScroll(rememberScrollState())) },
            confirmButton = {
                TextButton(onClick = { monitorDialog = null }) {
                    Text(stringResource(R.string.close))
                }
            },
        )
    }

    Column(Modifier.fillMaxWidth()) {
        if (saving) {
            LinearProgressIndicator(
                modifier = Modifier.fillMaxWidth().padding(bottom = 8.dp).height(2.dp),
            )
        }
        MonitorCard(title = stringResource(R.string.yukizygisk_injected_zygotes)) {
            if (monitoredZygotes.isEmpty()) {
                EmptyMonitorGroup(stringResource(R.string.yukizygisk_no_zygotes))
            } else {
                monitoredZygotes.forEachIndexed { index, zygote ->
                    if (index > 0) MonitorDivider()
                    val dialog = zygoteDialog(zygote)
                    ZygoteMonitorRow(zygote) {
                        monitorDialog = dialog
                    }
                }
            }
        }

        MonitorCard(title = stringResource(R.string.yukizygisk_modules)) {
            if (zygiskModules.isEmpty()) {
                EmptyMonitorGroup(stringResource(R.string.yukizygisk_no_modules))
            } else {
                zygiskModules.forEachIndexed { index, module ->
                    if (index > 0) MonitorDivider()
                    val dialog = zygiskModuleDialog(module)
                    ZygiskModuleRow(module) { monitorDialog = dialog }
                }
            }
        }

        MonitorCard(
            title = stringResource(R.string.yukizygisk_native_injections),
            trailing = {
                NativeMonitorModeToggle(
                    mode = nativeMonitorMode,
                    onClick = {
                        nativeMonitorMode = when (nativeMonitorMode) {
                            NativeMonitorMode.Module -> NativeMonitorMode.Process
                            NativeMonitorMode.Process -> NativeMonitorMode.Module
                        }
                    },
                )
            },
        ) {
            val moduleRows = remember(nativeModules, nativeInjections, crashEvidence) {
                buildNativeModuleRows(nativeModules, nativeInjections, crashEvidence)
            }
            val processRows = remember(nativeInjections) {
                nativeInjections
                    .groupBy { it.pid to it.process }
                    .map { (_, rows) ->
                        val first = rows.first()
                        val state = aggregateMonitorState(rows.map { it.state })
                        NativeProcessEntry(
                            pid = first.pid,
                            process = nativeProcessDisplayName(
                                first.process.ifEmpty { first.target }
                            ),
                            abi = first.abi,
                            modules = rows.map { it.module }.distinct(),
                            state = state,
                        )
                    }
                    .sortedWith(compareBy<NativeProcessEntry> { it.process }.thenBy { it.pid })
            }

            if (nativeMonitorMode == NativeMonitorMode.Module && moduleRows.isEmpty()) {
                EmptyMonitorGroup(stringResource(R.string.yukizygisk_no_native_modules))
            } else if (nativeMonitorMode == NativeMonitorMode.Process && processRows.isEmpty()) {
                EmptyMonitorGroup(stringResource(R.string.yukizygisk_no_native_injections))
            } else if (nativeMonitorMode == NativeMonitorMode.Module) {
                moduleRows.forEachIndexed { index, module ->
                    if (index > 0) MonitorDivider()
                    val dialog = nativeModuleDialog(module)
                    NativeModuleMonitorRow(module) {
                        monitorDialog = dialog
                    }
                }
            } else {
                processRows.forEachIndexed { index, process ->
                    if (index > 0) MonitorDivider()
                    val dialog = nativeProcessDialog(process)
                    NativeProcessMonitorRow(process) {
                        monitorDialog = dialog
                    }
                }
            }
        }

        InjectionConfiguration(config, configLoaded && !saving, ::save)
        InjectionDiagnostics(config, configLoaded && !saving, ::save)
    }
}

@Composable
private fun InjectionConfiguration(config: YzConfig, enabled: Boolean, onConfigChange: (YzConfig) -> Unit) {
    SettingsCard(title = stringResource(R.string.yukizygisk_module_loading)) {
        SwitchSettingItem(
            icon = Icons.Outlined.VisibilityOff,
            title = stringResource(R.string.yukizygisk_anonymous_memory_title),
            summary = stringResource(R.string.yukizygisk_anonymous_memory_summary),
            checked = config.anonymousMemory,
            enabled = enabled,
            groupPosition = MoreSettingsItemPosition.First,
            onChange = { onConfigChange(config.copy(anonymousMemory = it)) },
        )
        SwitchSettingItem(
            icon = Icons.Outlined.Link,
            title = stringResource(R.string.yukizygisk_yukilinker_title),
            summary = stringResource(R.string.yukizygisk_yukilinker_summary),
            checked = config.yukilinker,
            enabled = enabled,
            groupPosition = MoreSettingsItemPosition.Middle,
            onChange = { onConfigChange(config.copy(yukilinker = it)) },
        )
        SwitchSettingItem(
            icon = Icons.Filled.Bolt,
            title = stringResource(R.string.yukizygisk_early_load_title),
            summary = stringResource(R.string.yukizygisk_early_load_summary),
            checked = config.earlyLoad,
            enabled = enabled,
            groupPosition = MoreSettingsItemPosition.Middle,
            onChange = { onConfigChange(config.copy(earlyLoad = it)) },
        )
        SwitchSettingItem(
            icon = Icons.Outlined.Warning,
            title = stringResource(R.string.yukizygisk_crash_protection_title),
            summary = stringResource(R.string.yukizygisk_crash_protection_summary),
            checked = config.crashProtection,
            enabled = enabled,
            groupPosition = MoreSettingsItemPosition.Last,
            onChange = { onConfigChange(config.copy(crashProtection = it)) },
        )
    }
    SettingsCard(title = stringResource(R.string.injection_denylist_title)) {
        DenylistModeSelector(
            mode = config.denylistMode,
            enabled = enabled,
        ) { onConfigChange(config.copy(denylistMode = it)) }
    }
}

@Composable
private fun InjectionDiagnostics(config: YzConfig, enabled: Boolean, onConfigChange: (YzConfig) -> Unit) {
    SettingsCard(title = stringResource(R.string.yukizygisk_logging_title)) {
        SwitchSettingItem(
            icon = Icons.AutoMirrored.Filled.Article,
            title = stringResource(R.string.yukizygisk_log_dmesg_title),
            summary = stringResource(R.string.yukizygisk_log_dmesg_summary),
            checked = config.dmesgLog,
            enabled = enabled,
            groupPosition = MoreSettingsItemPosition.Only,
            onChange = { onConfigChange(config.copy(dmesgLog = it)) },
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun YukiZygiskTopBar(
    onBack: () -> Unit,
    backEnabled: Boolean = true,
    scrollBehavior: androidx.compose.material3.TopAppBarScrollBehavior,
) {
    YukiTopAppBar(
        title = { YukiTopBarTitle(stringResource(R.string.settings_yukizygisk)) },
        navigationIcon = {
            IconButton(onClick = onBack, enabled = backEnabled) {
                YukiIcon(Icons.AutoMirrored.Filled.ArrowBack, stringResource(R.string.back))
            }
        },
        scrollBehavior = scrollBehavior,
    )
}

@Composable
private fun ZygoteMonitorRow(zygote: ZygoteMonitorEntry, onStatusClick: () -> Unit) {
    MonitorRow(
        title = zygote.name,
        summary = stringResource(R.string.yukizygisk_zygote_detail, zygote.abi, zygote.pid),
        icon = Icons.Filled.Adb,
        state = zygote.state,
        onClick = onStatusClick,
    )
}

@Composable
private fun ZygiskModuleRow(module: ModuleDisplayEntry, onStatusClick: () -> Unit) {
    val warning = when {
        module.suspended -> stringResource(R.string.yukizygisk_module_suspended)
        module.crashEvidence.isNotEmpty() -> stringResource(R.string.yukizygisk_module_crash_badge)
        else -> null
    }
    MonitorRow(
        title = module.name,
        summary = module.id,
        icon = Icons.Filled.Extension,
        state = when {
            module.suspended -> MonitorState.Crashed
            module.state == MonitorState.Crashed -> MonitorState.Failed
            else -> module.state
        },
        warning = warning,
        statusDescription = if (module.suspended) warning else null,
        onClick = onStatusClick,
    )
}

@Composable
private fun NativeModuleMonitorRow(module: NativeModuleMonitorEntry, onStatusClick: () -> Unit) {
    MonitorRow(
        title = module.name,
        summary = module.id,
        icon = Icons.Filled.Extension,
        state = if (module.state == MonitorState.Crashed) MonitorState.Failed else module.state,
        warning = if (module.crashEvidence.isNotEmpty()) {
            stringResource(R.string.yukizygisk_module_crash_badge)
        } else null,
        onClick = onStatusClick,
    )
}

@Composable
private fun NativeProcessMonitorRow(process: NativeProcessEntry, onStatusClick: () -> Unit) {
    MonitorRow(
        title = process.process,
        summary = stringResource(R.string.yukizygisk_native_process_detail, process.abi, process.pid),
        icon = Icons.Filled.Terminal,
        state = process.state,
        onClick = onStatusClick,
    )
}

/** One surface belongs to the section; rows only provide content and a single action target. */
@Composable
private fun MonitorRow(
    title: String,
    summary: String,
    icon: ImageVector,
    state: MonitorState,
    onClick: () -> Unit,
    warning: String? = null,
    statusDescription: String? = null,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .heightIn(min = 72.dp)
            .clickable(role = Role.Button, onClick = onClick)
            .padding(horizontal = 16.dp, vertical = 12.dp),
        horizontalArrangement = Arrangement.spacedBy(16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        YukiIcon(icon, null, Modifier.size(24.dp), MaterialTheme.colorScheme.onSurfaceVariant)
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(title, style = MaterialTheme.typography.bodyLarge)
            Text(
                summary,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            if (warning != null) {
                Text(
                    warning,
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.error,
                )
            }
        }
        MonitorStateIcon(state, statusDescription)
    }
}

@Composable
private fun MonitorStateIcon(state: MonitorState, contentDescription: String? = null) {
    val icon: ImageVector
    val tint: Color
    val label: Int
    when (state) {
        MonitorState.Injected -> {
            icon = Icons.Outlined.TaskAlt
            tint = MaterialTheme.colorScheme.primary
            label = R.string.yukizygisk_native_scope_injected_no_process
        }
        MonitorState.Unsupported32 -> {
            icon = Icons.Outlined.Warning
            tint = MaterialTheme.colorScheme.tertiary
            label = R.string.yukizygisk_native_scope_unsupported
        }
        MonitorState.Crashed -> {
            icon = Icons.Outlined.Warning
            tint = MaterialTheme.colorScheme.error
            label = R.string.yukizygisk_native_scope_crashed
        }
        MonitorState.Failed -> {
            icon = Icons.Outlined.Cancel
            tint = MaterialTheme.colorScheme.error
            label = R.string.yukizygisk_native_scope_failed
        }
        MonitorState.Unknown -> {
            icon = Icons.AutoMirrored.Outlined.HelpOutline
            tint = MaterialTheme.colorScheme.onSurfaceVariant
            label = R.string.yukizygisk_native_scope_unobserved
        }
    }
    YukiIcon(icon, contentDescription ?: stringResource(label), Modifier.size(24.dp), tint)
}

@Composable
private fun NativeMonitorModeToggle(mode: NativeMonitorMode, onClick: () -> Unit) {
    TextButton(onClick = onClick, modifier = Modifier.heightIn(min = 48.dp)) {
        Row(
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                stringResource(
                    if (mode == NativeMonitorMode.Module) R.string.yukizygisk_native_mode_module
                    else R.string.yukizygisk_native_mode_process
                ),
                modifier = Modifier.weight(1f, fill = false),
                style = MaterialTheme.typography.labelLarge,
            )
            Icon(Icons.Filled.SwapHoriz, null, Modifier.size(24.dp))
        }
    }
}

@Composable
private fun MonitorCard(
    title: String,
    trailing: @Composable (() -> Unit)? = null,
    content: @Composable () -> Unit,
) {
    Column(Modifier.fillMaxWidth().padding(bottom = 16.dp)) {
        FlowRow(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 8.dp).heightIn(min = 48.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalArrangement = Arrangement.spacedBy(4.dp, Alignment.CenterVertically),
        ) {
            Text(
                title,
                modifier = Modifier.align(Alignment.CenterVertically).semantics { heading() },
                style = MaterialTheme.typography.titleSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            trailing?.invoke()
        }
        Surface(
            modifier = Modifier.fillMaxWidth(),
            shape = MaterialTheme.shapes.medium,
            color = MaterialTheme.colorScheme.surfaceContainerLow.copy(alpha = CardConfig.cardAlpha),
        ) {
            Column { content() }
        }
    }
}

@Composable
private fun MonitorDivider() {
    HorizontalDivider(
        modifier = Modifier.padding(start = 56.dp),
        color = MaterialTheme.colorScheme.outlineVariant,
    )
}

@Composable
private fun zygoteDialog(zygote: ZygoteMonitorEntry): MonitorDialogState {
    val resources = LocalResources.current
    val message = when (zygote.state) {
        MonitorState.Injected -> ""
        MonitorState.Unsupported32 -> stringResource(R.string.yukizygisk_zygote_unsupported_message)
        MonitorState.Crashed -> stringResource(R.string.yukizygisk_zygote_crashed_message)
        MonitorState.Failed -> stringResource(R.string.yukizygisk_zygote_failed_message)
        MonitorState.Unknown -> stringResource(R.string.yukizygisk_zygote_unknown_message)
    }
    val modules = if (!zygote.moduleMonitorAvailable) {
        resources.getString(R.string.yukizygisk_zygote_modules_unavailable)
    } else resources.getString(
        R.string.yukizygisk_zygote_modules_detail,
        zygote.modules.size,
        zygote.modules.joinToString("\n").ifBlank {
            resources.getString(R.string.yukizygisk_zygote_no_modules_detail)
        },
    )
    return MonitorDialogState(
        zygote.name,
        listOf(message, modules).filter(String::isNotBlank).joinToString("\n"),
    )
}

@Composable
private fun crashEvidenceMessage(evidence: List<CrashEvidence>): String {
    if (evidence.isEmpty()) return ""
    val resources = LocalResources.current
    val details = evidence.joinToString("\n\n") {
        resources.getString(
            R.string.yukizygisk_module_crash_evidence,
            it.process,
            it.pid,
            it.abi,
            it.timestamp,
            it.tombstone,
        ) + "\n" + it.frame
    }
    return resources.getString(R.string.yukizygisk_module_crash_explanation) + "\n\n" + details
}

@Composable
private fun zygiskModuleDialog(module: ModuleDisplayEntry): MonitorDialogState {
    val resources = LocalResources.current
    val abis = module.abis.joinToString(", ").ifBlank {
        resources.getString(R.string.yukizygisk_module_no_supported_abis)
    }
    val crash = crashEvidenceMessage(module.crashEvidence)
    return MonitorDialogState(
        module.name,
        listOf(
            resources.getString(R.string.yukizygisk_module_supported_abis, abis),
            if (module.suspended) resources.getString(R.string.yukizygisk_module_suspended_detail)
                else "",
            crash,
        ).filter { it.isNotBlank() }.joinToString("\n"),
    )
}

@Composable
private fun nativeProcessDialog(process: NativeProcessEntry): MonitorDialogState {
    val resources = LocalResources.current
    val modules = process.modules.joinToString("\n") {
        resources.getString(R.string.yukizygisk_native_process_module_line, it)
    }
    val base = when (process.state) {
        MonitorState.Injected -> stringResource(R.string.yukizygisk_native_process_injected_message)
        MonitorState.Unsupported32 ->
            stringResource(R.string.yukizygisk_native_process_unsupported_message)
        MonitorState.Crashed -> stringResource(R.string.yukizygisk_native_process_failed_message)
        MonitorState.Failed -> stringResource(R.string.yukizygisk_native_process_failed_message)
        MonitorState.Unknown -> stringResource(R.string.yukizygisk_native_process_unknown_message)
    }
    return MonitorDialogState(process.process, appendDetail(base, modules))
}

@Composable
private fun nativeModuleDialog(module: NativeModuleMonitorEntry): MonitorDialogState {
    val resources = LocalResources.current
    val scopes = module.scopes.joinToString("\n") { scope ->
        val status = when (scope.state) {
            MonitorState.Injected -> {
                val processes = scope.targets.joinToString(", ") {
                    resources.getString(R.string.yukizygisk_native_scope_process, it.process, it.pid)
                }
                if (processes.isBlank()) {
                    resources.getString(R.string.yukizygisk_native_scope_injected_no_process)
                } else {
                    resources.getString(R.string.yukizygisk_native_scope_injected, processes)
                }
            }
            MonitorState.Unsupported32 ->
                resources.getString(R.string.yukizygisk_native_scope_unsupported)
            MonitorState.Crashed -> resources.getString(R.string.yukizygisk_native_scope_crashed)
            MonitorState.Failed -> resources.getString(R.string.yukizygisk_native_scope_failed)
            MonitorState.Unknown -> resources.getString(R.string.yukizygisk_native_scope_unobserved)
        }
        val target = nativeProcessDisplayName(scope.target)
        val detail = if (scope.targets.isEmpty() && target.isNotBlank()) "$target: $status" else status
        resources.getString(R.string.yukizygisk_native_scope_status, detail)
    }
    val base = when (module.state) {
        MonitorState.Injected -> stringResource(R.string.yukizygisk_native_module_injected_message)
        MonitorState.Unsupported32 ->
            stringResource(R.string.yukizygisk_native_module_unsupported_message)
        MonitorState.Crashed -> stringResource(R.string.yukizygisk_native_module_failed_message)
        MonitorState.Failed -> stringResource(R.string.yukizygisk_native_module_failed_message)
        MonitorState.Unknown -> stringResource(R.string.yukizygisk_native_module_unknown_message)
    }
    return MonitorDialogState(
        module.name,
        appendDetail(appendDetail(base, scopes), crashEvidenceMessage(module.crashEvidence)),
    )
}

private fun appendDetail(base: String, detail: String): String =
    if (detail.isBlank()) base else "$base\n$detail"

@Composable
private fun EmptyMonitorGroup(text: String) {
    Text(
        text = text,
        style = MaterialTheme.typography.bodyMedium,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        modifier = Modifier.fillMaxWidth().defaultMinSize(minHeight = 64.dp)
            .padding(horizontal = 16.dp, vertical = 20.dp),
    )
}

@Composable
private fun DenylistModeSelector(
    mode: Int,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    onSelect: (Int) -> Unit,
) {
    val options = listOf(
        stringResource(R.string.yukizygisk_denylist_off),
        stringResource(R.string.yukizygisk_denylist_force),
        stringResource(R.string.yukizygisk_denylist_restore),
    )
    Column(modifier = modifier.fillMaxWidth().selectableGroup()) {
        options.forEachIndexed { index, label ->
            Row(
                modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp)
                    .selectable(selected = mode == index, enabled = enabled, role = Role.RadioButton,
                        onClick = { onSelect(index) })
                    .padding(horizontal = 16.dp, vertical = 8.dp),
                horizontalArrangement = Arrangement.spacedBy(16.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                RadioButton(selected = mode == index, onClick = null, enabled = enabled)
                Text(label, Modifier.weight(1f), style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = if (enabled) 1f else 0.5f))
            }
        }
    }
}

/** Uses production rows with sample data only: no configuration reads, polling or Root calls. */
@Preview(name = "YukiZygisk · light", widthDp = 360, heightDp = 740)
@Preview(name = "YukiZygisk · dark", widthDp = 360, heightDp = 740, uiMode = Configuration.UI_MODE_NIGHT_YES)
@Preview(name = "YukiZygisk · large text", widthDp = 320, heightDp = 900, fontScale = 2f)
@Preview(name = "YukiZygisk · landscape", widthDp = 800, heightDp = 360)
@Composable
private fun YukiZygiskMonitorPreview() {
    YukiZygiskPagePreview(enabled = true)
}

@Preview(name = "YukiZygisk · disabled", widthDp = 360, heightDp = 740)
@Preview(name = "YukiZygisk · disabled dark", widthDp = 360, heightDp = 740, uiMode = Configuration.UI_MODE_NIGHT_YES)
@Preview(name = "YukiZygisk · disabled large text", widthDp = 320, heightDp = 900, fontScale = 2f)
@Composable
private fun YukiZygiskDisabledPreview() {
    YukiZygiskPagePreview(enabled = false)
}

@Preview(name = "YukiZygisk · enabling", widthDp = 360, heightDp = 740)
@Composable
private fun YukiZygiskEnablingPreview() {
    YukiZygiskPagePreview(enabled = true, switching = true)
}

@Composable
private fun YukiZygiskPagePreview(enabled: Boolean, switching: Boolean = false) {
    UtilityPreviewTheme {
        Surface(color = MaterialTheme.colorScheme.background) {
            YukiZygiskPageLayout(
                enabled = enabled,
                switching = switching,
                masterSwitch = {
                    SwitchSettingItem(
                        icon = Icons.Filled.Extension,
                        title = stringResource(R.string.settings_yukizygisk),
                        summary = stringResource(R.string.settings_yukizygisk_summary),
                        checked = enabled,
                        enabled = !switching,
                        groupPosition = MoreSettingsItemPosition.Only,
                        onChange = {},
                    )
                },
            ) {
                MonitorCard(title = stringResource(R.string.yukizygisk_injected_zygotes)) {
                    ZygoteMonitorRow(ZygoteMonitorEntry(1854, "zygote", "arm64-v8a", MonitorState.Injected)) {}
                    MonitorDivider()
                    ZygoteMonitorRow(ZygoteMonitorEntry(1855, "zygote32", "armeabi-v7a", MonitorState.Unsupported32)) {}
                }
                MonitorCard(title = stringResource(R.string.yukizygisk_modules)) {
                    ZygiskModuleRow(
                        ModuleDisplayEntry(
                            name = "LSPosed · module with a long display name",
                            id = "org.example.zygisk.module.with.a.long.identifier",
                            abis = listOf("arm64-v8a"),
                            state = MonitorState.Injected,
                        ),
                    ) {}
                }
                MonitorCard(
                    title = stringResource(R.string.yukizygisk_native_injections),
                    trailing = { NativeMonitorModeToggle(NativeMonitorMode.Module) {} },
                ) {
                    NativeModuleMonitorRow(
                        NativeModuleMonitorEntry("LSPosed", "zygisk_lsposed", emptyList(), MonitorState.Injected, emptyList()),
                    ) {}
                }
                InjectionConfiguration(YzConfig(), enabled = true, onConfigChange = {})
                InjectionDiagnostics(YzConfig(), enabled = true, onConfigChange = {})
            }
        }
    }
}

@Preview(name = "Injection configuration · light", widthDp = 360, heightDp = 740)
@Preview(name = "Injection configuration · dark", widthDp = 360, heightDp = 740, uiMode = Configuration.UI_MODE_NIGHT_YES)
@Preview(name = "Injection configuration · large text", widthDp = 320, heightDp = 900, fontScale = 2f)
@Preview(name = "Injection configuration · landscape", widthDp = 800, heightDp = 360)
@Composable
private fun InjectionConfigurationPreview() {
    UtilityPreviewTheme {
        Surface(color = MaterialTheme.colorScheme.background) {
            Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp)) {
                InjectionConfiguration(YzConfig(), enabled = true, onConfigChange = {})
            }
        }
    }
}
