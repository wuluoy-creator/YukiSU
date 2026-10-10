package com.zying.zysu.ui.util.module

import com.zying.zysu.Natives
import com.google.gson.GsonBuilder
import com.google.gson.JsonArray
import com.google.gson.JsonElement
import com.google.gson.JsonNull
import com.google.gson.JsonObject
import com.google.gson.JsonPrimitive
import com.google.gson.Strictness
import com.google.gson.stream.JsonReader
import com.google.gson.stream.JsonToken
import java.io.InputStream
import java.io.StringReader
import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction

internal object AllowlistBackup {
    private const val FORMAT = "zysu.allowlist"
    private const val VERSION = 1
    private const val PROFILE_VERSION = 4
    private const val MAX_BYTES = 4 * 1024 * 1024
    private const val MAX_ENTRIES = 65534
    private val packagePattern = Regex("[a-zA-Z][a-zA-Z0-9_]*(\\.[a-zA-Z][a-zA-Z0-9_]*)*")
    private val integerPattern = Regex("-?(0|[1-9][0-9]*)")
    private val gson = GsonBuilder().serializeNulls().setPrettyPrinting().create()

    data class Document(val defaultUmountModules: Boolean, val apps: List<Entry>)
    data class Entry(val packages: List<String>, val profile: Natives.Profile)

    fun read(input: InputStream): Document {
        val output = java.io.ByteArrayOutputStream()
        val buffer = ByteArray(8192)
        while (true) {
            val count = input.read(buffer)
            if (count < 0) break
            require(output.size() + count <= MAX_BYTES) { "Backup exceeds 4 MiB" }
            output.write(buffer, 0, count)
        }
        val decoder = Charsets.UTF_8.newDecoder()
            .onMalformedInput(CodingErrorAction.REPORT).onUnmappableCharacter(CodingErrorAction.REPORT)
        return parse(decoder.decode(ByteBuffer.wrap(output.toByteArray())).toString())
    }

    fun encode(document: Document): String {
        validate(document)
        val root = JsonObject().apply {
            addProperty("format", FORMAT)
            addProperty("version", VERSION)
            addProperty("profileVersion", PROFILE_VERSION)
            addProperty("defaultUmountModules", document.defaultUmountModules)
            add("apps", JsonArray().apply {
                document.apps.sortedBy { it.profile.currentUid }.forEach { entry ->
                    val p = entry.profile
                    add(JsonObject().apply {
                        add("packages", strings(entry.packages.sorted()))
                        addProperty("appUid", p.currentUid)
                        addProperty("profileKey", p.name)
                        addProperty("allowSu", p.allowSu)
                        addProperty("rootUseDefault", p.rootUseDefault)
                        add("rootTemplate", p.rootTemplate?.let(::JsonPrimitive) ?: JsonNull.INSTANCE)
                        addProperty("rootUid", p.uid)
                        addProperty("rootGid", p.gid)
                        add("groups", numbers(p.groups))
                        add("capabilities", numbers(p.capabilities))
                        add("capabilitiesPermitted", numbers(p.capabilitiesPermitted))
                        add("capabilitiesInheritable", numbers(p.capabilitiesInheritable))
                        addProperty("context", p.context)
                        addProperty("namespace", p.namespace)
                        addProperty("flags", p.flags)
                        addProperty("nonRootUseDefault", p.nonRootUseDefault)
                        addProperty("umountModules", p.umountModules)
                        addProperty("sepolicyRules", p.rules)
                    })
                }
            })
        }
        return gson.toJson(root).also {
            require(it.toByteArray(Charsets.UTF_8).size <= MAX_BYTES) { "Backup exceeds 4 MiB" }
        }
    }

    fun parse(raw: String): Document {
        require(raw.toByteArray(Charsets.UTF_8).size <= MAX_BYTES) { "Backup exceeds 4 MiB" }
        val root = JsonReader(StringReader(raw)).use { reader ->
            reader.strictness = Strictness.STRICT
            val value = readValue(reader, 0)
            require(reader.peek() == JsonToken.END_DOCUMENT) { "Trailing JSON content" }
            value
        }.objectWithKeys("format", "version", "profileVersion", "defaultUmountModules", "apps")
        require(root.string("format") == FORMAT) { "Unsupported backup format; legacy backups are not supported" }
        require(root.int("version") == VERSION) { "Unsupported backup version" }
        require(root.int("profileVersion") == PROFILE_VERSION) { "Unsupported profile version" }
        return Document(root.boolean("defaultUmountModules"), root.array("apps").map { element ->
            val item = element.objectWithKeys(
                "packages", "appUid", "profileKey", "allowSu", "rootUseDefault", "rootTemplate",
                "rootUid", "rootGid", "groups", "capabilities", "capabilitiesPermitted",
                "capabilitiesInheritable", "context", "namespace", "flags", "nonRootUseDefault",
                "umountModules", "sepolicyRules"
            )
            Entry(item.array("packages").map { it.stringValue() }, Natives.Profile(
                name = item.string("profileKey"), currentUid = item.int("appUid"),
                allowSu = item.boolean("allowSu"), rootUseDefault = item.boolean("rootUseDefault"),
                rootTemplate = item["rootTemplate"].takeUnless { it.isJsonNull }?.stringValue(),
                uid = item.int("rootUid"), gid = item.int("rootGid"),
                groups = item.ints("groups"), capabilities = item.ints("capabilities"),
                capabilitiesPermitted = item.ints("capabilitiesPermitted"),
                capabilitiesInheritable = item.ints("capabilitiesInheritable"),
                context = item.string("context"), namespace = item.int("namespace"), flags = item["flags"].integer(),
                nonRootUseDefault = item.boolean("nonRootUseDefault"), umountModules = item.boolean("umountModules"),
                rules = item.string("sepolicyRules")
            ))
        }).also(::validate)
    }

    fun validateAgainst(document: Document, installed: Map<Int, List<String>>, protectedAppIds: Set<Int>) {
        validate(document)
        document.apps.forEach { entry ->
            val uid = entry.profile.currentUid
            require(uid % 100000 !in protectedAppIds) { "Manager UID $uid cannot be restored" }
            require(installed[uid]?.toSet() == entry.packages.toSet()) {
                "Package/UID or shared UID mismatch: $uid (${entry.packages.joinToString()})"
            }
        }
    }

    private fun validate(document: Document) {
        require(document.apps.size <= MAX_ENTRIES) { "Too many profiles" }
        val uids = mutableSetOf<Int>()
        val rules = mutableMapOf<String, String>()
        document.apps.forEach { entry ->
            val p = entry.profile
            require(p.currentUid == 1000 || p.currentUid >= 2000 && p.currentUid != 9999) { "Invalid app UID" }
            require(uids.add(p.currentUid)) { "Duplicate UID" }
            require(entry.packages.isNotEmpty() && entry.packages.size == entry.packages.distinct().size) { "Invalid package set" }
            entry.packages.forEach {
                require(it.length < 256 && packagePattern.matches(it)) { "Invalid package name" }
            }
            require(p.name in entry.packages) { "Profile key must belong to the UID's packages" }
            require(p.uid >= 0 && p.gid >= 0 && p.groups.all { it >= 0 }) { "Invalid root UID/GID" }
            require(p.groups.size <= 32 && p.groups.size == p.groups.distinct().size) { "Invalid supplementary groups" }
            listOf(p.capabilities, p.capabilitiesPermitted, p.capabilitiesInheritable).forEach {
                require(it.all { cap -> cap in 0..40 } && it.distinct().size == it.size) { "Invalid capabilities" }
            }
            require(p.context.isNotEmpty() && p.context.length < 64 && p.context.all { it.code in 33..126 }) { "Invalid SELinux context" }
            require(p.rootTemplate == null || p.rootTemplate.isNotEmpty() && p.rootTemplate.length < 256 && p.rootTemplate.all { it.code in 33..126 }) { "Invalid template ID" }
            require(p.namespace in 0..2) { "Invalid namespace" }
            require(p.flags and Natives.FLAG_KSU_NO_NEW_PRIVS.inv() == 0L) { "Unsupported profile flags" }
            require(p.rules.toByteArray(Charsets.UTF_8).size <= 65536 && '\u0000' !in p.rules) { "Invalid SELinux rules" }
            val previous = rules.put(p.name, p.rules)
            require(previous == null || previous == p.rules) { "Conflicting SELinux rules across users" }
            // The kernel stores root and non-root settings in a union.
            if (p.allowSu) {
                require(p.nonRootUseDefault && !p.umountModules) { "Root profile has non-root settings" }
                if (p.rootUseDefault) {
                    require(p.uid == 0 && p.gid == 0 && p.groups.isEmpty() &&
                        p.capabilities.isEmpty() && p.capabilitiesPermitted.isEmpty() &&
                        p.capabilitiesInheritable.isEmpty() && p.context == Natives.KERNEL_SU_DOMAIN &&
                        p.namespace == 0 && p.flags == 0L) { "Default root profile has custom parameters" }
                }
            } else {
                require(p.rootUseDefault && p.rootTemplate == null && p.uid == 0 && p.gid == 0 &&
                    p.groups.isEmpty() && p.capabilities.isEmpty() && p.capabilitiesPermitted.isEmpty() &&
                    p.capabilitiesInheritable.isEmpty() && p.context == Natives.KERNEL_SU_DOMAIN &&
                    p.namespace == 0 && p.flags == 0L) { "Non-root profile has root parameters" }
                require(!p.nonRootUseDefault || !p.umountModules) { "Default non-root profile has custom umount" }
            }
        }
    }

    private fun readValue(reader: JsonReader, depth: Int): JsonElement {
        require(depth <= 8) { "JSON nesting too deep" }
        return when (reader.peek()) {
            JsonToken.BEGIN_OBJECT -> JsonObject().apply {
                reader.beginObject()
                while (reader.hasNext()) {
                    val name = reader.nextName()
                    require(!has(name)) { "Duplicate JSON field: $name" }
                    add(name, readValue(reader, depth + 1))
                }
                reader.endObject()
            }
            JsonToken.BEGIN_ARRAY -> JsonArray().apply {
                reader.beginArray()
                while (reader.hasNext()) {
                    require(size() < MAX_ENTRIES) { "JSON array too large" }
                    add(readValue(reader, depth + 1))
                }
                reader.endArray()
            }
            JsonToken.STRING -> JsonPrimitive(reader.nextString())
            JsonToken.NUMBER -> {
                val token = reader.nextString()
                require(integerPattern.matches(token)) { "Expected integer" }
                JsonPrimitive(token.toLong())
            }
            JsonToken.BOOLEAN -> JsonPrimitive(reader.nextBoolean())
            JsonToken.NULL -> { reader.nextNull(); JsonNull.INSTANCE }
            else -> error("Invalid JSON value")
        }
    }

    private fun strings(values: List<String>) = JsonArray().apply { values.forEach { add(it) } }
    private fun numbers(values: List<Int>) = JsonArray().apply { values.forEach { add(it) } }
    private fun JsonElement.objectWithKeys(vararg keys: String): JsonObject {
        require(isJsonObject && asJsonObject.keySet() == keys.toSet()) { "Backup schema mismatch" }
        return asJsonObject
    }
    private fun JsonElement.stringValue(): String {
        require(isJsonPrimitive && asJsonPrimitive.isString) { "Expected string" }
        return asString
    }
    private fun JsonElement.integer(): Long {
        require(isJsonPrimitive && asJsonPrimitive.isNumber) { "Expected integer" }
        return asLong
    }
    private fun JsonObject.string(name: String) = get(name).stringValue()
    private fun JsonObject.boolean(name: String): Boolean {
        val value = get(name)
        require(value.isJsonPrimitive && value.asJsonPrimitive.isBoolean) { "Expected boolean" }
        return value.asBoolean
    }
    private fun JsonObject.int(name: String): Int = get(name).integer().let {
        require(it in Int.MIN_VALUE..Int.MAX_VALUE) { "Integer out of range" }
        it.toInt()
    }
    private fun JsonObject.array(name: String): List<JsonElement> {
        require(get(name).isJsonArray) { "Expected array" }
        return get(name).asJsonArray.toList()
    }
    private fun JsonObject.ints(name: String): List<Int> = array(name).map {
        val number = it.integer()
        require(number in 0..Int.MAX_VALUE) { "Integer out of range" }
        number.toInt()
    }
}
