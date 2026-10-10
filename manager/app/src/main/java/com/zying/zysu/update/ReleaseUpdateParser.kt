package com.zying.zysu.update

import com.google.gson.JsonObject
import com.google.gson.JsonParser
import com.zying.zysu.ui.util.module.LatestVersionInfo
import java.net.URI

/** Parses only published assets belonging to the release returned by our GitHub API. */
internal object ReleaseUpdateParser {
    private const val REPOSITORY = "wuluoy-creator/ZySU"
    private const val MAX_VERSION_CODE = 2_100_000_000L
    private val legacyApkName = Regex("^ZySU_.+_([0-9]+)(?:-arm64-v8a)?-release\\.apk$")
    private val sha256 = Regex("[0-9a-fA-F]{64}")

    fun parseRelease(body: String): Release? = runCatching {
        val root = JsonParser.parseString(body).asJsonObject
        if (root.get("draft")?.asBoolean == true || root.get("prerelease")?.asBoolean == true) {
            return null
        }
        val tag = root.string("tag_name")?.takeIf(String::isNotBlank) ?: return null
        val assets = root.getAsJsonArray("assets").mapNotNull { element ->
            runCatching {
                val asset = element.asJsonObject
                val name = asset.string("name") ?: return@runCatching null
                if (name.isBlank() || '/' in name || '\\' in name) return@runCatching null
                val url = asset.string("browser_download_url") ?: return@runCatching null
                if (!isReleaseAssetUrl(url, tag, name)) return@runCatching null
                val size = if (asset.has("size")) {
                    asset.integer("size")?.takeIf { it > 0 } ?: return@runCatching null
                } else {
                    null
                }
                Asset(name, url, size)
            }.getOrNull()
        }
        Release(
            tag = tag,
            changelog = root.string("body").orEmpty().replace("\\r\\n", "\n"),
            assets = assets,
        )
    }.getOrNull()

    internal class Release(
        private val tag: String,
        private val changelog: String,
        private val assets: List<Asset>,
    ) {
        val metadataUrl: String?
            get() = assets.singleOrNull { it.name == "update.json" }?.url

        fun latestVersion(metadataBody: String? = null): LatestVersionInfo? {
            if (metadataBody != null) {
                runCatching { parseMetadata(metadataBody) }.getOrNull()?.let { return it }
            }
            return assets.mapNotNull { asset ->
                val match = legacyApkName.matchEntire(asset.name) ?: return@mapNotNull null
                val code = match.groupValues[1].toLongOrNull()
                    ?.takeIf { it in 1..MAX_VERSION_CODE } ?: return@mapNotNull null
                versionInfo(code, asset, tag)
            }.maxByOrNull { it.versionCode }
        }

        private fun parseMetadata(body: String): LatestVersionInfo? {
            val root = JsonParser.parseString(body).asJsonObject
            if (root.integer("schema_version") != 1L) return null
            val code = root.integer("version_code")
                ?.takeIf { it in 1..MAX_VERSION_CODE } ?: return null
            val apk = root.getAsJsonObject("apk") ?: return null
            val name = apk.string("name")?.takeIf { it.endsWith(".apk") } ?: return null
            val asset = assets.singleOrNull { it.name == name } ?: return null
            if (apk.string("sha256")?.matches(sha256) != true) return null
            val size = apk.integer("size")?.takeIf { it > 0 } ?: return null
            if (asset.size != null && size != asset.size) return null
            val versionName = root.string("version_name")?.takeIf(String::isNotBlank) ?: tag
            return versionInfo(code, asset, versionName)
        }

        private fun versionInfo(code: Long, asset: Asset, name: String) = LatestVersionInfo(
            versionCode = code.toInt(),
            downloadUrl = asset.url,
            changelog = changelog,
            versionName = name.removePrefix("v"),
        )
    }

    internal data class Asset(val name: String, val url: String, val size: Long?)

    private fun isReleaseAssetUrl(url: String, tag: String, name: String): Boolean =
        runCatching {
            val uri = URI(url)
            uri.scheme == "https" && uri.host == "github.com" &&
                uri.port in setOf(-1, 443) && uri.userInfo == null &&
                uri.query == null && uri.fragment == null &&
                uri.path == "/$REPOSITORY/releases/download/$tag/$name" &&
                uri.path.split('/').none { it == "." || it == ".." }
        }.getOrDefault(false)

    private fun JsonObject.string(key: String): String? = get(key)
        ?.takeIf { it.isJsonPrimitive && it.asJsonPrimitive.isString }
        ?.asString

    // Do not coerce fractional numbers, overflow, booleans, or quoted strings to version codes.
    private fun JsonObject.integer(key: String): Long? = get(key)
        ?.takeIf { it.isJsonPrimitive && it.asJsonPrimitive.isNumber }
        ?.asString
        ?.takeIf { it.matches(Regex("[0-9]+")) }
        ?.toLongOrNull()
}
