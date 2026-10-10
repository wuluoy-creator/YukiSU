package com.zying.zysu.ui.util

import android.app.Activity
import android.app.ActivityManager
import android.os.Build
import com.zying.zysu.R

fun Activity.setTaskDescriptionLabel(label: String) {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
        @Suppress("DEPRECATION")
        setTaskDescription(ActivityManager.TaskDescription(label))
    } else {
        val taskDescription = ActivityManager.TaskDescription.Builder()
            .setLabel(label)
            .build()
        setTaskDescription(taskDescription)
    }
}

fun Activity.resetTaskDescriptionToAppName() {
    setTaskDescriptionLabel(getString(R.string.app_name))
}
