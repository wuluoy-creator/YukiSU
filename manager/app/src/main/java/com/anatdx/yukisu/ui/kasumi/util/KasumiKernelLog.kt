package com.anatdx.yukisu.ui.kasumi.util

import java.util.ArrayDeque

private val kasumiKernelPrefix = Regex(
    "^\\s*(?:<\\d+>)?\\s*(?:\\[\\s*\\d+(?:\\.\\d+)?]\\s*)?" +
        "(?:\\[\\s*[TC]\\d+]\\s*)?KernelSU: kasumi:(?:\\s|$)",
)

internal fun filterKasumiKernelLog(content: String): String {
    val lines = ArrayDeque<String>(1000)
    content.lineSequence().forEach { line ->
        if (kasumiKernelPrefix.containsMatchIn(line)) {
            if (lines.size == 1000) lines.removeFirst()
            lines.addLast(line)
        }
    }
    return lines.joinToString("\n")
}
