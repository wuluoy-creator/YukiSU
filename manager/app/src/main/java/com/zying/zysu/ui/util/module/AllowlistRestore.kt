package com.zying.zysu.ui.util.module

import com.zying.zysu.Natives

internal object AllowlistRestore {
    const val DEFAULT_UID = 9999
    const val DEFAULT_KEY = "$"

    interface Store {
        fun read(uid: Int): Natives.Profile?
        fun write(profile: Natives.Profile): Boolean
        fun readRules(key: String): String
        fun writeRules(key: String, rules: String): Boolean
    }

    class Failure(val rollbackFailed: Boolean, cause: Exception) : Exception(
        if (rollbackFailed) "Restore failed and rollback is incomplete" else "Restore failed; previous settings restored", cause
    )

    fun apply(
        document: AllowlistBackup.Document,
        before: Map<Int, Natives.Profile>,
        protectedAppIds: Set<Int>,
        store: Store
    ) {
        val desired = document.apps.associate { it.profile.currentUid to it.profile }.toMutableMap()
        before.filterKeys {
            it != DEFAULT_UID && it !in desired && it % 100000 !in protectedAppIds
        }.forEach { (uid, profile) ->
            desired[uid] = Natives.Profile(name = profile.name, currentUid = uid)
        }
        desired[DEFAULT_UID] = Natives.Profile(
            name = DEFAULT_KEY, currentUid = DEFAULT_UID, nonRootUseDefault = false,
            umountModules = document.defaultUmountModules
        )
        require(desired.keys.none { it % 100000 in protectedAppIds }) {
            "Protected manager profile cannot be restored"
        }
        val targetRules = document.apps.associate { it.profile.name to it.profile.rules }
        val ruleKeys = before.values
            .filter { it.currentUid != DEFAULT_UID && it.currentUid % 100000 !in protectedAppIds }
            .map { it.name }
            .toSet() + targetRules.keys
        val oldRules = ruleKeys.associateWith(store::readRules)
        val rules = ruleKeys.associateWith { key ->
            targetRules[key].orEmpty()
        }
        val attemptedProfiles = mutableListOf<Int>()
        val attemptedRules = mutableListOf<String>()
        try {
            rules.forEach { (key, value) ->
                if (value != oldRules.getValue(key)) {
                    attemptedRules += key
                    check(store.writeRules(key, value)) { "Cannot write SELinux rules for $key" }
                    check(store.readRules(key) == value) { "SELinux rules verification failed for $key" }
                }
            }
            desired.forEach { (uid, profile) ->
                val target = profile.copy(rules = "")
                if (before[uid]?.copy(rules = "") != target) {
                    attemptedProfiles += uid
                    check(store.write(target)) { "Cannot update UID $uid" }
                    check(store.read(uid)?.copy(rules = "") == target) { "Profile verification failed for UID $uid" }
                }
            }
        } catch (failure: Exception) {
            var rollbackFailed = false
            attemptedProfiles.asReversed().forEach { uid ->
                // An absent entry behaves like a non-root profile using the global default.
                val previous = before[uid]?.copy(rules = "") ?: Natives.Profile(
                    name = desired.getValue(uid).name, currentUid = uid,
                    nonRootUseDefault = uid != DEFAULT_UID
                )
                val restored = runCatching {
                    store.write(previous) && store.read(uid)?.copy(rules = "") == previous
                }.getOrDefault(false)
                if (!restored) rollbackFailed = true
            }
            attemptedRules.asReversed().forEach { key ->
                val previous = oldRules.getValue(key)
                val restored = runCatching {
                    store.writeRules(key, previous) && store.readRules(key) == previous
                }.getOrDefault(false)
                if (!restored) rollbackFailed = true
            }
            throw Failure(rollbackFailed, failure)
        }
    }
}
