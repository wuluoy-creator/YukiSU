package com.zying.zysu

import kotlin.test.Test
import kotlin.test.assertEquals

class KernelsTest {
    @Test
    fun parsesAndroidRelease() {
        assertEquals(KernelVersion(6, 1, 75),
            parseKernelVersion("6.1.75-android14-11-g16c5f6cd5e9b-ab12268515"))
    }

    @Test
    fun customReleaseCannotOverflowVersionComponents() {
        val unknown = KernelVersion(-1, -1, -1)
        for (release in listOf("99999999999999999999.1.1", "6.99999999999999999999.1",
            "6.1.99999999999999999999", "custom-kernel")) {
            assertEquals(unknown, parseKernelVersion(release))
        }
    }
}
