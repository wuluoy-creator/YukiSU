package com.anatdx.yukisu.ui.util

import android.content.Context
import androidx.core.content.edit
import com.anatdx.yukisu.Natives
import com.anatdx.yukisu.ksuApp

private const val SETTINGS_PREFS = "settings"
private const val KEY_USE_SOFT_REBOOT = "soft_reboot"

fun isSoftRebootEnabled(context: Context = ksuApp): Boolean =
    context.getSharedPreferences(SETTINGS_PREFS, Context.MODE_PRIVATE)
        .getBoolean(KEY_USE_SOFT_REBOOT, false)

fun setSoftRebootEnabled(context: Context, enabled: Boolean) {
    context.getSharedPreferences(SETTINGS_PREFS, Context.MODE_PRIVATE).edit {
        putBoolean(KEY_USE_SOFT_REBOOT, enabled)
    }
}

fun isSoftRebootPreferred(context: Context = ksuApp): Boolean =
    !isSoftRebootBlockedByKasumi() &&
        (runCatching { Natives.isLateLoadMode }.getOrDefault(false) || isSoftRebootEnabled(context))

fun isSoftRebootBlockedByKasumi(): Boolean {
    val runtime = runCatching { Natives.kasumiRuntimeState() }.getOrDefault(-1)
    if (runtime > 0) return true
    val enabled = runCatching { Natives.getFeature(Natives.FEATURE_KASUMI) }.getOrDefault(-1)
    val available = runCatching { Natives.kasumiIsInitialized() }.getOrDefault(false)
    return enabled > 0 || (runtime < 0 && available)
}
