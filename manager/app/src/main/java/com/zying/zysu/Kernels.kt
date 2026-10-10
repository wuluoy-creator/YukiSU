package com.zying.zysu

import android.system.Os

/**
 * @author weishu
 * @date 2022/12/10.
 */

data class KernelVersion(val major: Int, val patchLevel: Int, val subLevel: Int) {
    override fun toString(): String = "$major.$patchLevel.$subLevel"
}

fun parseKernelVersion(version: String): KernelVersion {
    val unknown = KernelVersion(-1, -1, -1)
    val find = "(\\d+)\\.(\\d+)\\.(\\d+)".toRegex().find(version)
        ?: return unknown
    val major = find.groupValues[1].toIntOrNull() ?: return unknown
    val patchLevel = find.groupValues[2].toIntOrNull() ?: return unknown
    val subLevel = find.groupValues[3].toIntOrNull() ?: return unknown
    return KernelVersion(major, patchLevel, subLevel)
}

fun getKernelVersion(): KernelVersion {
    Os.uname().release.let {
        return parseKernelVersion(it)
    }
}
