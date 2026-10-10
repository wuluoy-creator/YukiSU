package com.zying.zysu.ui.util

import android.content.Context
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.util.Log
import androidx.core.content.FileProvider
import com.zying.zysu.ksuApp
import com.zying.zysu.ui.util.module.LatestVersionInfo
import com.zying.zysu.update.ReleaseUpdateParser
import okhttp3.CacheControl
import okhttp3.Call
import okhttp3.Callback
import okhttp3.Request
import okhttp3.Response
import java.io.File
import java.io.IOException

private const val TAG = "DownloadUtil"
private val mainHandler by lazy { Handler(Looper.getMainLooper()) }

data class DownloadProgress(
    val downloadedBytes: Long = 0L,
    val totalBytes: Long? = null,
) {
    val fraction: Float?
        get() = totalBytes?.takeIf { it > 0L }
            ?.let { (downloadedBytes.toDouble() / it.toDouble()).toFloat().coerceIn(0f, 1f) }
}

fun interface DownloadHandle {
    fun cancel()
}

/**
 * @author weishu
 * @date 2023/6/22.
 */
fun download(
    context: Context,
    url: String,
    fileName: String,
    description: String,
    maxBytes: Long? = null,
    requireSecureTransport: Boolean = false,
    onDownloaded: (Uri) -> Unit = {},
    onProgress: (DownloadProgress) -> Unit = {},
    onError: (String) -> Unit = {}
): DownloadHandle {
    Log.d(TAG, "Start Download: $url")
    val appContext = context.applicationContext
    val request = Request.Builder()
        .url(url)
        .cacheControl(CacheControl.Builder().noStore().build())
        .header("Accept", "application/zip, application/octet-stream")
        .build()
    val call = ksuApp.okhttpClient.newCall(request)

    call.enqueue(object : Callback {
        override fun onFailure(call: Call, e: IOException) {
            if (!call.isCanceled()) deliverDownloadError(url, e, onError)
        }

        override fun onResponse(call: Call, response: Response) {
            var partialFile: File? = null
            try {
                response.use {
                    check(response.isSuccessful) { "HTTP ${response.code} for $url" }
                    val finalUrl = response.request.url
                    val loopbackHttp = finalUrl.scheme == "http" &&
                        finalUrl.host in setOf("127.0.0.1", "0.0.0.0", "::1", "[::1]")
                    check(!requireSecureTransport || finalUrl.isHttps || loopbackHttp) {
                        "Download redirected to an insecure URL"
                    }
                    val body = checkNotNull(response.body) { "Empty response body for $url" }
                    val downloadDir = File(appContext.cacheDir, "module_downloads")
                    check(downloadDir.isDirectory || downloadDir.mkdirs()) {
                        "Cannot create the module download cache"
                    }
                    val targetFile = File(downloadDir, safeDownloadFileName(fileName))
                    val temporaryFile = File(
                        downloadDir,
                        ".${targetFile.name}.${System.nanoTime()}.part",
                    )
                    partialFile = temporaryFile
                    val contentLength = body.contentLength().takeIf { it >= 0L }
                    check(maxBytes == null || contentLength == null || contentLength <= maxBytes) {
                        "Download exceeds the size limit"
                    }
                    body.byteStream().use { input ->
                        temporaryFile.outputStream().buffered().use { output ->
                            copyDownloadWithProgress(
                                input = input,
                                output = output,
                                contentLength = contentLength,
                                maxBytes = maxBytes,
                                onProgress = onProgress,
                            )
                        }
                    }
                    if (targetFile.exists()) {
                        check(targetFile.delete()) { "Cannot replace ${targetFile.name}" }
                    }
                    check(temporaryFile.renameTo(targetFile)) { "Cannot finalize ${targetFile.name}" }
                    partialFile = null

                    val uri = FileProvider.getUriForFile(
                        appContext,
                        "${appContext.packageName}.fileprovider",
                        targetFile,
                    )
                    Log.d(TAG, "Downloaded $description to ${targetFile.absolutePath}")
                    mainHandler.post { onDownloaded(uri) }
                }
            } catch (error: Exception) {
                if (!call.isCanceled()) deliverDownloadError(url, error, onError)
            } finally {
                partialFile?.delete()
            }
        }
    })
    return DownloadHandle(call::cancel)
}

internal fun safeDownloadFileName(fileName: String): String {
    val candidate = File(fileName).name
        .replace(Regex("[^A-Za-z0-9._() -]"), "_")
        .trim()
    return candidate.takeUnless { it.isEmpty() || it == "." || it == ".." } ?: "module.zip"
}

private fun deliverDownloadError(url: String, error: Exception, onError: (String) -> Unit) {
    Log.e(TAG, "Failed to download $url", error)
    val detail = error.message?.takeIf(String::isNotBlank) ?: error.javaClass.simpleName
    mainHandler.post { onError(detail) }
}

private fun copyDownloadWithProgress(
    input: java.io.InputStream,
    output: java.io.OutputStream,
    contentLength: Long?,
    maxBytes: Long?,
    onProgress: (DownloadProgress) -> Unit,
) {
    val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
    var downloaded = 0L
    var lastPercent = -1
    var lastUnknownLengthUpdate = 0L

    fun report(force: Boolean = false) {
        val percent = contentLength?.takeIf { it > 0L }
            ?.let { ((downloaded * 100L) / it).toInt().coerceIn(0, 100) }
        val now = System.nanoTime()
        val shouldReport = force || if (percent == null) {
            now - lastUnknownLengthUpdate >= 250_000_000L
        } else {
            percent != lastPercent
        }
        if (!shouldReport) return
        if (percent == null) lastUnknownLengthUpdate = now else lastPercent = percent
        val progress = DownloadProgress(downloaded, contentLength)
        mainHandler.post { onProgress(progress) }
    }

    report(force = true)
    while (true) {
        val count = input.read(buffer)
        if (count < 0) break
        check(maxBytes == null || downloaded + count <= maxBytes) {
            "Download exceeds the size limit"
        }
        output.write(buffer, 0, count)
        downloaded += count
        report()
    }
    report(force = true)
}

fun checkNewVersion(): LatestVersionInfo {
    val url = "https://api.github.com/repos/wuluoy-creator/ZySU/releases/latest"
    val defaultValue = LatestVersionInfo()
    return runCatching {
        val release = ReleaseUpdateParser.parseRelease(requestReleaseJson(url, 1024L * 1024))
            ?: return defaultValue
        val metadata = release.metadataUrl?.let { metadataUrl ->
            runCatching { requestReleaseJson(metadataUrl, 256L * 1024) }
                .onFailure { Log.w("CheckUpdate", "Release metadata unavailable; checking APK name", it) }
                .getOrNull()
        }
        release.latestVersion(metadata) ?: defaultValue
    }.onFailure { Log.w("CheckUpdate", "Failed to check release update", it) }
        .getOrDefault(defaultValue)
}

private fun requestReleaseJson(url: String, maxBytes: Long): String {
    val request = Request.Builder().url(url).build()
    return ksuApp.okhttpClient.newCall(request).execute().use { response ->
        check(response.isSuccessful) { "HTTP ${response.code} for release metadata" }
        check(response.request.url.isHttps) { "Release metadata redirected to an insecure URL" }
        val body = checkNotNull(response.body) { "Empty release metadata response" }
        check(body.contentLength() <= maxBytes) { "Release metadata exceeds the size limit" }
        val source = body.source()
        source.request(maxBytes + 1)
        check(source.buffer.size <= maxBytes) { "Release metadata exceeds the size limit" }
        source.readUtf8()
    }
}
