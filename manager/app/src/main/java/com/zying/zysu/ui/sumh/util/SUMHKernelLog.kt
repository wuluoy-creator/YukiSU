package com.zying.zysu.ui.sumh.util

import java.util.ArrayDeque

private val sumhKernelPrefix = Regex(
    "^\\s*(?:<\\d+>)?\\s*(?:\\[\\s*\\d+(?:\\.\\d+)?]\\s*)?" +
        "(?:\\[\\s*[TC]\\d+]\\s*)?KernelSU: sumh:(?:\\s|$)",
)

internal fun filterSUMHKernelLog(content: String): String {
    val lines = ArrayDeque<String>(1000)
    content.lineSequence().forEach { line ->
        if (sumhKernelPrefix.containsMatchIn(line)) {
            if (lines.size == 1000) lines.removeFirst()
            lines.addLast(line)
        }
    }
    return lines.joinToString("\n")
}
