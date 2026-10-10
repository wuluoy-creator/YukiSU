package com.zying.zysu.ui.sumh.util

import android.util.Log
import com.zying.zysu.Natives
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.util.Locale

object SUMHUiAdapter {
    enum class SUMHStatus { AVAILABLE, NOT_PRESENT, KERNEL_TOO_OLD, MODULE_TOO_OLD }
    data class FeaturesResult(val bitmask: Int, val names: List<String>)
    data class ModuleInfo(val id: String, val name: String, val mode: String, val strategy: String)
    data class ActiveRule(val type: String, val src: String, val target: String? = null, val isUserDefined: Boolean = false,
        val hideState: Int? = null, val hideError: Int = 0)
    data class PartitionInfo(val name: String)
    data class MountStats(
        val totalMounts: Int, val overlayfsMounts: Int,
    )
    data class SystemInfo(
        val kernel: String, val mountBase: String,
        val activeMounts: List<String>, val sumhModuleIds: List<String>,
        val sumhMismatch: Boolean, val mismatchMessage: String?,
        val mountStats: MountStats? = null, val detectedPartitions: List<PartitionInfo> = emptyList(),
        val hooks: String = "", val viewsEnabled: Boolean? = null,
    )
    data class StorageInfo(val size: String, val used: String, val avail: String, val percent: String, val type: String)
    data class SUMHPConfig(
        val tempdir: String = "",
        val mountsource: String = "KSU", val debug: Boolean = false, val verbose: Boolean = false,
        val fsType: String = "auto", val enableKernelDebug: Boolean = false,
        val enableStealth: Boolean = true, val enableOverlayXattrHide: Boolean = false,
        val enableMountHide: Boolean = false,
        val enableMapsSpoof: Boolean = false, val enableStatfsSpoof: Boolean = false,
        val enableKernelBuildSpoof: Boolean = false,
        val kernelBuildRelease: String = "", val kernelBuildVersion: String = "",
        val kernelBuildApplyStage: String = KERNEL_BUILD_EARLY_STAGE,
        val mirrorPath: String = "", val partitions: List<String> = emptyList(),
        val mountBackend: String = "auto", val overlayfsEnabled: Boolean = true, val magicMountEnabled: Boolean = true,
        val kernelAvailable: Boolean = false, val externalOwner: String = "",
        val original: JSONObject = JSONObject(), val supported: Set<String> = emptySet(),
    )
    data class UiState(
        val version: String, val status: SUMHStatus, val config: SUMHPConfig,
        val modules: List<ModuleInfo>, val system: SystemInfo, val storage: StorageInfo,
        val rules: List<ActiveRule>, val features: FeaturesResult?,
    )

    private fun config(raw: JSONObject, names: Set<String>, available: Boolean, externalOwner: String): SUMHPConfig {
        val partitions = raw.optJSONArray("partitions")
        return SUMHPConfig(
            tempdir = raw.optString("work_dir", "/dev/sumhp").takeUnless { it == "/dev/sumhp" }.orEmpty(),
            mountsource = raw.optString("mountsource", "KSU"),
            debug = raw.optBoolean("debug"), verbose = raw.optBoolean("verbose"),
            fsType = raw.optString("fs_type", "auto"), enableKernelDebug = raw.optBoolean("enable_kernel_debug"),
            enableStealth = raw.optBoolean("enable_stealth", true),
            enableOverlayXattrHide = raw.optBoolean("enable_overlay_xattr_hide"),
            enableMountHide = raw.optBoolean("enable_mount_hide"),
            enableMapsSpoof = raw.optBoolean("enable_maps_spoof"),
            enableStatfsSpoof = raw.optBoolean("enable_statfs_spoof"),
            enableKernelBuildSpoof = raw.optBoolean("enable_kernel_build_spoof"),
            kernelBuildRelease = raw.optString("kernel_build_release"),
            kernelBuildVersion = raw.optString("kernel_build_version"),
            kernelBuildApplyStage = raw.optString("kernel_build_apply_stage", KERNEL_BUILD_EARLY_STAGE),
            mirrorPath = raw.optString("mirror_dir"),
            partitions = if (partitions == null) emptyList() else (0 until partitions.length()).map(partitions::getString),
            mountBackend = raw.optString("mount_backend", "auto"),
            overlayfsEnabled = raw.optBoolean("overlayfs_enabled", true), magicMountEnabled = raw.optBoolean("magic_mount_enabled", true),
            kernelAvailable = available, externalOwner = externalOwner,
            original = raw, supported = names,
        )
    }

    private fun rules(raw: String, userRules: Set<String>): List<ActiveRule> = raw.lineSequence().filter(String::isNotBlank).mapNotNull { line ->
        val fields = line.trim().split(Regex("\\s+"), limit = 3)
        val type = fields[0].lowercase(Locale.ROOT)
        if (type == "inject" || type == "sumh") return@mapNotNull null
        val path = if (type == "hide") line.substringAfter(' ', "") else fields.getOrElse(1) { line }
        val source = fields.getOrNull(2)?.let { if (type == "add") it.replace(Regex("\\s+-?\\d+$"), "") else it }
        ActiveRule(type, path, if (type in listOf("add", "merge")) source else null,
            type == "hide" && path in userRules)
    }.toList()

    private fun userRules(snapshot: SUMHManager.RuleSnapshot): List<ActiveRule> {
        val registered = snapshot.system.optJSONArray("user_hide") ?: return snapshot.hideRules.map {
            ActiveRule("hide", it, isUserDefined = true)
        }
        val live = (0 until registered.length()).map(registered::getJSONObject).associateBy { it.getString("path") }
        return (snapshot.hideRules + live.keys).distinct().map { path ->
            val state = live[path]
            ActiveRule("hide", path, isUserDefined = true,
                hideState = when {
                    state == null -> -1
                    state.optInt("management") == 1 -> 4
                    else -> state.optInt("binding")
                }, hideError = state?.optInt("error") ?: 0)
        }
    }

    private fun activeRules(snapshot: SUMHManager.RuleSnapshot): List<ActiveRule> {
        val legacy = rules(snapshot.system.optString("rules"), if (snapshot.system.has("user_hide")) emptySet() else snapshot.hideRules.toSet())
        return if (snapshot.system.has("user_hide")) legacy + userRules(snapshot) else legacy
    }

    private fun size(value: Long): String {
        val units = listOf("B", "KiB", "MiB", "GiB", "TiB")
        var amount = value.toDouble()
        var unit = 0
        while (amount >= 1024 && unit < units.lastIndex) { amount /= 1024; unit++ }
        return String.format(Locale.ROOT, "%.1f %s", amount, units[unit])
    }

    suspend fun load(): UiState = withContext(Dispatchers.IO) {
        val snapshot = SUMHManager.snapshot()
        val sys = snapshot.system
        val protocol = sys.optInt("kernel_version")
        val status = when {
            snapshot.mounts.available -> SUMHStatus.AVAILABLE
            protocol == 0 -> SUMHStatus.NOT_PRESENT
            protocol < 1 -> SUMHStatus.KERNEL_TOO_OLD
            else -> SUMHStatus.MODULE_TOO_OLD
        }
        val features = sys.optJSONObject("features") ?: JSONObject()
        val names = features.optJSONArray("names")
        val supported = if (names == null) emptyList() else (0 until names.length()).map(names::getString)
        val viewConfig = config(snapshot.config, supported.toSet(), snapshot.mounts.available, snapshot.mounts.externalOwner)
        val base = sys.optString("mount_base")
        val disk = try {
            val path = base.ifBlank { "/data/adb/ksu/sumhp" }
            JSONObject(Natives.sumhStorageInfo(path.toByteArray(Charsets.UTF_8)).toString(Charsets.UTF_8))
        } catch (error: Exception) {
            if (error is CancellationException) throw error
            Log.w("SUMHUiAdapter", "Storage information unavailable", error)
            JSONObject()
        }
        val total = disk.optLong("size", -1)
        val avail = disk.optLong("avail", -1)
        val used = (total - avail).coerceAtLeast(0)
        val storage = if (total < 0) StorageInfo("—", "—", "—", "—", "unknown") else StorageInfo(
            size(total), size(used), size(avail), "${if (total == 0L) 0 else (100.0 * used / total).toInt()}%", disk.optString("type", "unknown"),
        )
        val active = sys.optJSONArray("active_mounts")
        val mounts = if (active == null) emptyList() else (0 until active.length()).map(active::getString)
        val parts = sys.optJSONArray("detectedPartitions")
        val partitions = if (parts == null) emptyList() else (0 until parts.length()).map {
            PartitionInfo(parts.getJSONObject(it).getString("name"))
        }
        val rawRules = sys.optString("rules")
        val ids = Regex("/data/adb/modules/([^/\\s]+)").findAll(rawRules).map { it.groupValues[1] }.distinct().toList()
        val stats = sys.optJSONObject("mountStats")
        UiState(
            protocol.toString(), status, viewConfig,
            snapshot.mounts.modules.values.map { ModuleInfo(it.id, it.name, it.mode, it.strategy) },
            SystemInfo(sys.optString("kernel"), base, mounts, ids,
                protocol != 0 && protocol != 1, null,
                stats?.let { MountStats(it.optInt("total_mounts"), it.optInt("overlayfs_mounts")) }, partitions,
                sys.optString("hooks"), sys.optBoolean("enabled")),
            storage, activeRules(SUMHManager.RuleSnapshot(sys, snapshot.hideRules)),
            FeaturesResult(features.optInt("bitmask"), supported),
        )
    }

    private suspend fun success(action: suspend () -> Unit): Boolean = try {
        action(); true
    } catch (error: Exception) {
        if (error is CancellationException) throw error
        Log.w("SUMHUiAdapter", "Controller operation failed", error)
        false
    }

    suspend fun saveConfig(value: SUMHPConfig): SUMHManager.ConfigSaveResult {
        val old = config(value.original, value.supported, value.kernelAvailable, value.externalOwner)
        val updates = JSONObject()
        fun change(key: String, before: Any, after: Any) { if (before != after) updates.put(key, after) }
        if (old.tempdir != value.tempdir) updates.put("work_dir", value.tempdir.ifEmpty { "/dev/sumhp" })
        change("mountsource", old.mountsource, value.mountsource)
        change("debug", old.debug, value.debug)
        change("verbose", old.verbose, value.verbose)
        change("fs_type", old.fsType, value.fsType)
        change("enable_kernel_debug", old.enableKernelDebug, value.enableKernelDebug)
        change("enable_stealth", old.enableStealth, value.enableStealth)
        change("enable_overlay_xattr_hide", old.enableOverlayXattrHide, value.enableOverlayXattrHide)
        change("enable_mount_hide", old.enableMountHide, value.enableMountHide)
        // Mount hiding always uses normal mode; normalize older saved levels on save.
        change("mount_hide_mode", value.original.optString("mount_hide_mode", "normal"), "normal")
        change("enable_maps_spoof", old.enableMapsSpoof, value.enableMapsSpoof)
        change("enable_statfs_spoof", old.enableStatfsSpoof, value.enableStatfsSpoof)
        change("enable_kernel_build_spoof", old.enableKernelBuildSpoof, value.enableKernelBuildSpoof)
        change("kernel_build_release", old.kernelBuildRelease, value.kernelBuildRelease)
        change("kernel_build_version", old.kernelBuildVersion, value.kernelBuildVersion)
        change("kernel_build_apply_stage", old.kernelBuildApplyStage, value.kernelBuildApplyStage)
        change("mirror_dir", old.mirrorPath, value.mirrorPath)
        change("mount_backend", old.mountBackend, value.mountBackend)
        change("overlayfs_enabled", old.overlayfsEnabled, value.overlayfsEnabled)
        change("magic_mount_enabled", old.magicMountEnabled, value.magicMountEnabled)
        if (old.partitions != value.partitions) updates.put("partitions", org.json.JSONArray(value.partitions))
        if (updates.length() == 0) return SUMHManager.ConfigSaveResult(false, false, noChanges = true)
        return SUMHManager.saveAndApplyConfig(updates, value.kernelAvailable)
    }

    suspend fun getActiveRules(): List<ActiveRule> = withContext(Dispatchers.IO) {
        val snapshot = SUMHManager.ruleSnapshot()
        activeRules(snapshot)
    }
    suspend fun listUserHideRules(): List<String> = SUMHManager.listHideRules()
    suspend fun userHideStates(): List<ActiveRule> {
        val snapshot = SUMHManager.ruleSnapshot()
        val storedPaths = snapshot.hideRules.toHashSet()
        return userRules(snapshot).filter { it.src in storedPaths }
    }
    suspend fun retryUserHide(path: String) = success { SUMHManager.retryUserHide(path) }
    suspend fun addUserHideRule(path: String) = success { SUMHManager.saveHide(path) }
    suspend fun removeUserHideRule(path: String) = success { SUMHManager.saveHide(path, true) }
    suspend fun clearAllRules() = success { SUMHManager.clearRules() }
    suspend fun clearMapsRules() = success { SUMHManager.clearMapsRules() }
    suspend fun addMapsRule(ti: Long, td: Long, si: Long, sd: Long, path: String) = success {
        SUMHManager.addMapsRule(listOf(ti, td, si, sd).map { it.toULong().toString() }, path)
    }
    suspend fun readLog(): String = SUMHManager.readLog()
    suspend fun readKernelLog(): String = withContext(Dispatchers.IO) {
        filterSUMHKernelLog(Natives.sumhReadLog(true).toString(Charsets.UTF_8))
    }
    suspend fun clearLog() = success { SUMHManager.clearLog() }
    suspend fun retryApply() = success { SUMHManager.applyConfig() }
    suspend fun syncPartitionsWithDaemon(): SUMHPConfig? = try {
        SUMHManager.syncPartitions(); load().config
    } catch (error: Exception) {
        if (error is CancellationException) throw error
        null
    }
}
