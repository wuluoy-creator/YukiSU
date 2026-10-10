package com.zying.zysu.ui.util.module

import com.zying.zysu.Natives
import com.google.gson.JsonParser
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFails
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class AllowlistBackupTest {
    private fun entry(uid: Int = 10123, name: String = "test.app") =
        AllowlistBackup.Entry(listOf(name), Natives.Profile(name, uid))
    private fun document(vararg entries: AllowlistBackup.Entry) = AllowlistBackup.Document(true, entries.toList())
    private fun json() = AllowlistBackup.encode(document(entry()))

    @Test fun roundTripRetainsExplicitDefaultDenyCustomRootAndGlobalUmount() {
        val custom = entry(10124, "test.root").let { it.copy(profile = it.profile.copy(
            allowSu = true, rootUseDefault = false, rootTemplate = "test.template", uid = 1000, gid = 1000,
            groups = listOf(0, 1000, 3003), capabilities = listOf(0, 1, 40),
            capabilitiesPermitted = listOf(0, 40), capabilitiesInheritable = listOf(1),
            context = "u:r:su:s0", namespace = 2, flags = 1,
            rules = "allow su app_data_file file read;"
        )) }
        val unmount = entry(10125, "test.hide").let { it.copy(profile = it.profile.copy(
            nonRootUseDefault = false, umountModules = true
        )) }
        val original = document(entry(), custom, unmount)
        assertEquals(original, AllowlistBackup.parse(AllowlistBackup.encode(original)))
    }

    @Test fun samePackageInTwoUsersIsValid() {
        val backup = document(entry(), entry(1010123))
        val parsed = AllowlistBackup.parse(AllowlistBackup.encode(backup))
        AllowlistBackup.validateAgainst(parsed, mapOf(10123 to listOf("test.app"), 1010123 to listOf("test.app")), emptySet())
        assertEquals(2, parsed.apps.size)
    }

    @Test fun sharedUidRequiresExactMembershipButAllowsExistingKeyToDiffer() {
        val backup = document(entry().copy(packages = listOf("test.app", "test.shared")))
        AllowlistBackup.validateAgainst(backup, mapOf(10123 to listOf("test.shared", "test.app")), emptySet())
        assertFails { AllowlistBackup.validateAgainst(backup, mapOf(10123 to listOf("test.app")), emptySet()) }
        assertFails { AllowlistBackup.validateAgainst(backup, mapOf(10123 to listOf("test.app", "test.shared", "test.other")), emptySet()) }
    }

    @Test fun missingWrongUidAndManagerUidAreRejected() {
        val backup = document(entry())
        assertFails { AllowlistBackup.validateAgainst(backup, emptyMap(), emptySet()) }
        assertFails { AllowlistBackup.validateAgainst(backup, mapOf(10124 to listOf("test.app")), emptySet()) }
        assertFails { AllowlistBackup.validateAgainst(backup, mapOf(10123 to listOf("test.app")), setOf(10123)) }
        assertFails { AllowlistBackup.validateAgainst(document(entry(1010123)), mapOf(1010123 to listOf("test.app")), setOf(10123)) }
    }

    @Test fun malformedLegacyDuplicateAndNonJsonSyntaxAreRejected() {
        listOf("\u007fKSU", "{}", "[]", json() + "{}", json().replace("\"version\": 1", "\"version\": 1, \"version\": 1"),
            json().replace("\"version\": 1", "version: 1"), json().replace("\"format\":", "/*comment*/ \"format\":"))
            .forEach { assertFails(it.take(50)) { AllowlistBackup.parse(it) } }
    }

    @Test fun unknownMissingAndWrongTypedFieldsAreRejected() {
        listOf("dynamicManagers", "arbitrary").forEach { key ->
            val root = JsonParser.parseString(json()).asJsonObject
            root.addProperty(key, true)
            assertFails { AllowlistBackup.parse(root.toString()) }
        }
        val root = JsonParser.parseString(json()).asJsonObject
        root.remove("defaultUmountModules")
        assertFails { AllowlistBackup.parse(root.toString()) }
        assertFails { AllowlistBackup.parse(json().replace("\"allowSu\": false", "\"allowSu\": \"false\"")) }
        assertFails { AllowlistBackup.parse(json().replace("\"version\": 1", "\"version\": 2")) }
    }

    @Test fun unsafeNumbersStringsAndUnionParametersAreRejected() {
        listOf("2147483648", "-1", "1.0", "1e3", "18446744073709551617").forEach {
            assertFails { AllowlistBackup.parse(json().replace("\"appUid\": 10123", "\"appUid\": $it")) }
        }
        listOf("\"context\": \"u:r:su:s0\"" to "\"context\": \"bad\\u0000context\"",
            "\"flags\": 0" to "\"flags\": 2", "\"rootUid\": 0" to "\"rootUid\": 1",
            "\"umountModules\": false" to "\"umountModules\": true",
            "\"profileKey\": \"test.app\"" to "\"profileKey\": \"../escape\"")
            .forEach { (old, new) -> assertFails { AllowlistBackup.parse(json().replace(old, new)) } }
    }

    @Test fun duplicateUidOrSharedPolicyConflictsAreRejected() {
        assertFails { AllowlistBackup.encode(document(entry(), entry())) }
        assertFails { AllowlistBackup.encode(document(entry(), entry(1010123).let {
            it.copy(profile = it.profile.copy(rules = "different"))
        })) }
    }

    @Test fun streamRejectsExcessSizeAndMalformedUtf8() {
        assertFails { AllowlistBackup.read(ByteArray(4 * 1024 * 1024 + 1).inputStream()) }
        assertFails { AllowlistBackup.read(byteArrayOf(0xc3.toByte(), 0x28).inputStream()) }
    }

    @Test fun exportNeverContainsManagerTrustFields() {
        val output = json()
        assertFalse(output.contains("dynamic", ignoreCase = true))
        assertFalse(output.contains("signature", ignoreCase = true))
        assertTrue(output.contains("defaultUmountModules"))
    }
}
