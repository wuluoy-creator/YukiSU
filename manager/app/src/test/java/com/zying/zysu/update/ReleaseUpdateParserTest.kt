package com.zying.zysu.update

import com.google.gson.JsonArray
import com.google.gson.JsonObject
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull

class ReleaseUpdateParserTest {
    @Test
    fun usesMetadataVersionAndOnlyTheNamedPublishedAsset() {
        val release = release(
            asset("ZySU_v1.8.0_10481-release.apk"),
            asset("app-release.apk"),
            asset("update.json"),
        )
        val parsed = assertNotNull(ReleaseUpdateParser.parseRelease(release.toString()))
        assertEquals(url("update.json"), parsed.metadataUrl)
        val update = assertNotNull(parsed.latestVersion(metadata().toString()))

        assertEquals(12000, update.versionCode)
        assertEquals("1.8.2", update.versionName)
        assertEquals(url("app-release.apk"), update.downloadUrl)
        assertEquals("First line\nSecond line", update.changelog)
    }

    @Test
    fun missingMetadataSupportsOldAndAbiReleaseNames() {
        for (name in listOf("ZySU_v1.8.0_11000-release.apk", "ZySU_v1.8.0_11000-arm64-v8a-release.apk")) {
            val parsed = assertNotNull(ReleaseUpdateParser.parseRelease(release(asset(name)).toString()))
            val update = assertNotNull(parsed.latestVersion())
            assertEquals(11000, update.versionCode)
            assertEquals("1.8.0", update.versionName)
        }
    }

    @Test
    fun invalidMetadataFallsBackToACompatibleReleaseName() {
        val parsed = assertNotNull(ReleaseUpdateParser.parseRelease(release(
            asset("app-release.apk"), asset("update.json"), asset("ZySU_v1.8.0_11000-release.apk"),
        ).toString()))
        val invalid = metadata().apply { addProperty("version_code", 0) }
        assertEquals(11000, assertNotNull(parsed.latestVersion(invalid.toString())).versionCode)
        assertEquals(11000, assertNotNull(parsed.latestVersion("not JSON")).versionCode)
    }

    @Test
    fun metadataCannotReferenceMissingAssetsOrSupplyItsOwnDownloadUrl() {
        val body = metadata().apply {
            getAsJsonObject("apk").addProperty("name", "missing.apk")
            getAsJsonObject("apk").addProperty("url", "https://example.com/app.apk")
        }
        assertNull(parseMetadata(body))
    }

    @Test
    fun metadataRequiresAnIntegerAndroidVersionCodeInRange() {
        for (code in listOf("0", "-1", "2100000001", "4294979296", "12000.5", "\"12000\"", "true")) {
            val body = metadata().toString().replace("\"version_code\":12000", "\"version_code\":$code")
            val release = assertNotNull(ReleaseUpdateParser.parseRelease(
                release(asset("app-release.apk"), asset("update.json")).toString()
            ))
            assertNull(release.latestVersion(body), "version code $code")
        }
        assertEquals(2_100_000_000, assertNotNull(parseMetadata(metadata().apply {
            addProperty("version_code", 2_100_000_000)
        })).versionCode)
    }

    @Test
    fun metadataRequiresSupportedSchemaChecksumAndMatchingSize() {
        assertNull(parseMetadata(metadata().apply { addProperty("schema_version", 2) }))
        assertNull(parseMetadata(metadata().apply { getAsJsonObject("apk").addProperty("sha256", "bad") }))
        assertNull(parseMetadata(metadata().apply { getAsJsonObject("apk").addProperty("size", 9) }))
        assertNull(parseMetadata(metadata().apply { getAsJsonObject("apk").remove("size") }))
    }

    @Test
    fun metadataCanUseTagAsDisplayNameAndAnAssetWithoutOptionalSize() {
        val release = assertNotNull(ReleaseUpdateParser.parseRelease(release(
            asset("app-release.apk").apply { remove("size") }, asset("update.json"),
        ).toString()))
        val body = metadata().apply { remove("version_name") }
        assertEquals("1.8.0", assertNotNull(release.latestVersion(body.toString())).versionName)
    }

    @Test
    fun releaseAssetUrlsMustBelongToTheExpectedRepositoryTagAndFile() {
        val name = "ZySU_v1.8.0_11000-release.apk"
        for (badUrl in listOf(
            url(name).replace("https://", "http://"),
            url(name).replace("github.com", "github.com.example.com"),
            url(name).replace("wuluoy-creator", "someone"),
            url(name).replace("/ZySU/", "/ZySU-Releases/"),
            url(name).replace("/v1.8.0/", "/v1.7.0/"),
            url(name).replace(name, "other.apk"),
            url(name) + "?redirect=other",
        )) {
            val parsed = assertNotNull(ReleaseUpdateParser.parseRelease(release(
                asset(name).apply { addProperty("browser_download_url", badUrl) },
            ).toString()))
            assertNull(parsed.latestVersion(), badUrl)
        }
    }

    @Test
    fun metadataIsFetchedOnlyFromItsTrustedReleaseAssetUrl() {
        val parsed = assertNotNull(ReleaseUpdateParser.parseRelease(release(
            asset("update.json").apply { addProperty("browser_download_url", "https://example.com/update.json") },
        ).toString()))
        assertNull(parsed.metadataUrl)
    }

    @Test
    fun fallbackIgnoresArbitraryApksDebugBuildsAndOverflowingVersions() {
        for (name in listOf(
            "app-release.apk", "Other_v1_11000-release.apk", "ZySU_v1_11000-debug.apk",
            "ZySU_v1_11000-release.apk.bak.apk", "ZySU_v1_2100000001-release.apk",
            "ZySU_v1_99999999999999999999999-release.apk",
        )) {
            val parsed = assertNotNull(ReleaseUpdateParser.parseRelease(release(asset(name)).toString()))
            assertNull(parsed.latestVersion(), name)
        }
    }

    @Test
    fun malformedOrNonStableReleasesAreIgnored() {
        for (body in listOf("not JSON", "{}", "[]")) {
            assertNull(ReleaseUpdateParser.parseRelease(body))
        }
        for (flag in listOf("draft", "prerelease")) {
            assertNull(ReleaseUpdateParser.parseRelease(release().apply { addProperty(flag, true) }.toString()))
        }
    }

    private fun parseMetadata(body: JsonObject) = assertNotNull(ReleaseUpdateParser.parseRelease(
        release(asset("app-release.apk"), asset("update.json")).toString()
    )).latestVersion(body.toString())

    private fun release(vararg assets: JsonObject) = JsonObject().apply {
        addProperty("tag_name", "v1.8.0")
        addProperty("body", "First line\nSecond line")
        add("assets", JsonArray().apply { assets.forEach { add(it) } })
    }

    private fun asset(name: String) = JsonObject().apply {
        addProperty("name", name)
        addProperty("browser_download_url", url(name))
        addProperty("size", 100)
    }

    private fun metadata() = JsonObject().apply {
        addProperty("schema_version", 1)
        addProperty("version_code", 12000)
        addProperty("version_name", "v1.8.2")
        add("apk", JsonObject().apply {
            addProperty("name", "app-release.apk")
            addProperty("sha256", "ab".repeat(32))
            addProperty("size", 100)
        })
    }

    private fun url(name: String) = "https://github.com/wuluoy-creator/ZySU/releases/download/v1.8.0/$name"
}
