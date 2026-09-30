package com.anatdx.yukisu.ui.kasumi.util

import com.anatdx.yukisu.Natives
import com.anatdx.yukisu.ui.util.getKsud
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject

object KasumiManager {
    val mountModes = listOf("auto", "kasumi", "overlay", "magic", "none")
    val ruleModes = mountModes + "hide"
    private val writes = Mutex()
    private val changes = MutableStateFlow(0L)
    val revision = changes.asStateFlow()

    data class ModuleRule(val path: String, val mode: String)
    data class ConfigSaveResult(val persisted: Boolean, val applied: Boolean, val error: String? = null, val noChanges: Boolean = false)
    data class MountConflict(val path: String, val modules: List<String>)
    data class ModuleInfo(
        val id: String, val mode: String, val rules: List<ModuleRule>,
        val name: String = id, val strategy: String = "none",
    )
    data class MountState(
        val modules: Map<String, ModuleInfo>,
        val available: Boolean,
        val globalMode: String,
        val externalOwner: String,
        val builtinEnabled: Boolean = true,
        val activeKasumiIds: Set<String> = emptySet(),
    )
    data class Snapshot(
        val system: JSONObject,
        val config: JSONObject,
        val activeRules: JSONArray,
        val hideRules: List<String>,
        val mounts: MountState,
    )

    private fun command(vararg args: String): String {
        val request = JSONArray(args.toList()).toString().toByteArray(Charsets.UTF_8)
        val response = JSONObject(Natives.kagamiRequest(getKsud().toByteArray(Charsets.UTF_8), request).toString(Charsets.UTF_8))
        check(response.getBoolean("ok") && response.getInt("exit_code") == 0) {
            response.optString("stderr").ifBlank { "Kagami error ${response.optInt("errno")}" }.take(1500)
        }
        return response.getString("stdout").trim()
    }

    private fun kernelSnapshot() = JSONObject(Natives.kasumiKernelSnapshot().toString(Charsets.UTF_8))

    private fun objectResult(vararg args: String): JSONObject {
        val output = command(*args)
        val start = output.indexOf('{')
        check(start >= 0) { "Invalid Kagami response" }
        return JSONObject(output.substring(start))
    }

    private fun arrayResult(vararg args: String): JSONArray {
        val output = command(*args)
        return JSONArray(output)
    }

    internal fun parseModules(root: JSONObject): Map<String, ModuleInfo> {
        val modules = root.getJSONArray("modules")
        return (0 until modules.length()).associate { i ->
            val module = modules.getJSONObject(i)
            val entries = module.optJSONArray("rules") ?: JSONArray()
            val info = ModuleInfo(
                module.getString("id"), module.getString("mode"),
                (0 until entries.length()).map { index ->
                    val rule = entries.getJSONObject(index)
                    ModuleRule(rule.getString("path"), rule.getString("mode"))
                },
                module.optString("name", module.getString("id")), module.optString("strategy", "none"),
            )
            info.id to info
        }
    }

    private fun readMountState(config: JSONObject, kernel: JSONObject): MountState {
        val meta = objectResult("api", "meta", "--control-only")
        check(meta.optBoolean("embedded") && meta.optBoolean("native_control")) {
            "Update ksud and restart the Kagami controller"
        }
        return MountState(
            parseModules(objectResult("module", "list", "--all")), kernel.getBoolean("kasumi_available"),
            config.optString("mount_backend", "auto"),
            meta.optString("external_mount_owner"),
            config.optBoolean("builtin_mount_enabled", true),
            if (kernel.optBoolean("enabled"))
                Regex("/data/adb/modules/([^/\\s]+)").findAll(kernel.optString("rules")).map { it.groupValues[1] }.toSet()
            else emptySet(),
        )
    }

    suspend fun getMountState(): MountState = withContext(Dispatchers.IO) {
        val config = objectResult("config", "show", "--stored")
        readMountState(config, kernelSnapshot())
    }

    suspend fun snapshot(): Snapshot = withContext(Dispatchers.IO) {
        val system = kernelSnapshot()
        val config = objectResult("config", "show", "--stored")
        val available = system.getBoolean("kasumi_available")
        val mountState = readMountState(config, system)
        val mounts = objectResult("api", "mounts")
        mounts.keys().forEach { key -> system.put(key, mounts.get(key)) }
        val hides = arrayResult("hide", "list")
        Snapshot(
            system, config,
            JSONArray(system.optString("rules").lineSequence().filter(String::isNotBlank)
                .map { JSONObject().put("args", it) }.toList()),
            (0 until hides.length()).map(hides::getString),
            mountState,
        )
    }

    private suspend fun mutate(block: () -> Unit) = withContext(NonCancellable + Dispatchers.IO) {
        writes.withLock {
            try { block() } finally { changes.value++ }
        }
    }

    suspend fun saveConfig(updates: JSONObject) = mutate {
        command("config", "merge-json", updates.toString())
    }

    suspend fun saveAndApplyConfig(updates: JSONObject, applyRuntime: Boolean): ConfigSaveResult =
        withContext(NonCancellable + Dispatchers.IO) {
            writes.withLock {
                var persisted = false
                try {
                    command("config", "merge-json", updates.toString())
                    persisted = true
                    if (applyRuntime) command("config", "apply")
                    ConfigSaveResult(persisted = true, applied = applyRuntime)
                } catch (error: Exception) {
                    ConfigSaveResult(persisted, false, error.message ?: "Configuration operation failed")
                } finally { changes.value++ }
            }
        }

    suspend fun checkConflicts(): List<MountConflict> = withContext(Dispatchers.IO) {
        val entries = arrayResult("module", "check-conflicts")
        (0 until entries.length()).map { index ->
            val entry = entries.getJSONObject(index)
            val modules = entry.getJSONArray("modules")
            MountConflict(entry.getString("file"), (0 until modules.length()).map(modules::getString))
        }
    }

    suspend fun clearLog() = mutate { Natives.kasumiClearLog() }

    suspend fun applyConfig() = mutate { command("config", "apply") }

    suspend fun saveModule(id: String, mode: String, rules: List<ModuleRule>) = mutate {
        require(mode in mountModes)
        require(rules.all { it.path.startsWith('/') && it.mode in ruleModes })
        require(rules.map { it.path }.distinct().size == rules.size)
        val meta = objectResult("api", "meta", "--control-only")
        check(meta.optString("external_mount_owner").isEmpty()) { "An external metamodule owns mounting" }
        val previous = parseModules(objectResult("module", "list", "--all"))[id]
        check(previous != null) { "Module is no longer mountable" }
        command("module", "set-mode", id, mode)
        previous.rules.filter { old -> rules.none { it.path == old.path } }.forEach {
            command("module", "remove-rule", id, it.path)
        }
        rules.filter { it !in previous.rules }.forEach {
            command("module", "add-rule", id, it.path, it.mode)
        }
    }

    suspend fun saveHide(path: String, remove: Boolean = false) = mutate {
        require(path.startsWith('/') && path != "/system/bin/su")
        command("hide", if (remove) "remove" else "add", path)
    }

    suspend fun clearRules() = mutate { command("kasumi", "clear") }
    suspend fun retryUserHide(path: String) = mutate {
        Natives.kasumiRetryUserHide(path.toByteArray(Charsets.UTF_8))
    }
    suspend fun addMapsRule(numbers: List<String>, path: String) = mutate {
        require(numbers.size == 4 && numbers.all { it.toULongOrNull() != null } && path.startsWith('/'))
        Natives.kasumiAddMapsRule(numbers.map { it.toULong().toLong() }.toLongArray(), path.toByteArray(Charsets.UTF_8))
    }
    suspend fun clearMapsRules() = mutate { Natives.kasumiClearMapsRules() }
    suspend fun resetRecovery() = mutate { command("recovery", "reset") }

    suspend fun readLog(): String = withContext(Dispatchers.IO) {
        Natives.kasumiReadLog(false).toString(Charsets.UTF_8)
    }

    internal suspend fun readConfig(): JSONObject = withContext(Dispatchers.IO) {
        objectResult("config", "show", "--stored")
    }

    internal suspend fun syncPartitions() = mutate { command("config", "sync-partitions") }

    suspend fun isBuiltinMountEnabled(): Boolean = readConfig().optBoolean("builtin_mount_enabled", true)
    suspend fun setBuiltinMountEnabled(enabled: Boolean) = saveConfig(JSONObject().put("builtin_mount_enabled", enabled))
}
