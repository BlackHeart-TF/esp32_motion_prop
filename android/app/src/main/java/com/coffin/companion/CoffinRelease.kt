package com.coffin.companion

import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.util.zip.ZipInputStream

/**
 * Latest release asset [ZIP_NAME] holds coffin.bin plus wavs in screams/ and creaks/.
 * One folder wrapped around those is fine.
 */
object CoffinRelease {
    const val OWNER = ""
    const val REPO = ""
    const val ZIP_NAME = "coffin.zip"
    const val BIN_NAME = "coffin.bin"

    private const val MAX_BYTES = 48L * 1024 * 1024
    private const val MAX_ENTRIES = 256

    fun pull(filesDir: File, onProgress: (PullProgress) -> Unit): PullResult {
        if (OWNER.isBlank() || REPO.isBlank()) {
            throw ReleaseException(ReleaseException.Reason.NO_REPO)
        }
        onProgress(PullProgress.Checking)
        val release = getJson("https://api.github.com/repos/$OWNER/$REPO/releases/latest")
        val tag = release.optString("tag_name").ifBlank { "release" }
        val url = zipUrl(release) ?: throw ReleaseException(ReleaseException.Reason.NO_ZIP)
        val zipFile = File(filesDir, "coffin-download.zip")
        try {
            download(url, zipFile, onProgress)
            onProgress(PullProgress.Unpacking)
            val counts = unpack(zipFile, File(filesDir, "firmware"))
            return PullResult(tag, counts.first, counts.second)
        } finally {
            zipFile.delete()
        }
    }

    private fun getJson(url: String): JSONObject {
        val conn = open(url, json = true)
        try {
            when (conn.responseCode) {
                404 -> throw ReleaseException(ReleaseException.Reason.NO_RELEASE)
                in 200..299 -> return JSONObject(conn.inputStream.bufferedReader().use { it.readText() })
                else -> throw ReleaseException(ReleaseException.Reason.FAILED, "HTTP ${conn.responseCode}")
            }
        } finally {
            conn.disconnect()
        }
    }

    private fun zipUrl(release: JSONObject): String? {
        val assets = release.optJSONArray("assets") ?: return null
        for (i in 0 until assets.length()) {
            val asset = assets.getJSONObject(i)
            if (asset.optString("name").equals(ZIP_NAME, ignoreCase = true)) {
                val url = asset.optString("browser_download_url")
                if (url.isNotBlank()) {
                    return url
                }
            }
        }
        return null
    }

    private fun download(url: String, dest: File, onProgress: (PullProgress) -> Unit) {
        val conn = open(url, json = false)
        try {
            val code = conn.responseCode
            if (code == 404) {
                throw ReleaseException(ReleaseException.Reason.NO_ZIP)
            }
            if (code !in 200..299) {
                throw ReleaseException(ReleaseException.Reason.FAILED, "HTTP $code")
            }
            val total = conn.contentLengthLong
            var got = 0L
            var reported = -1L
            dest.outputStream().use { out ->
                conn.inputStream.use { input ->
                    val buf = ByteArray(8192)
                    while (true) {
                        val n = input.read(buf)
                        if (n < 0) {
                            break
                        }
                        got += n
                        if (got > MAX_BYTES) {
                            throw ReleaseException(ReleaseException.Reason.TOO_BIG)
                        }
                        out.write(buf, 0, n)
                        if (reported < 0 || got - reported >= 128 * 1024 || (total > 0 && got >= total)) {
                            reported = got
                            onProgress(PullProgress.Downloading(got, total))
                        }
                    }
                }
            }
        } finally {
            conn.disconnect()
        }
    }

    private fun unpack(zipFile: File, dest: File): Pair<Int, Int> {
        val names = ArrayList<String>()
        ZipInputStream(zipFile.inputStream().buffered()).use { zip ->
            while (true) {
                val entry = zip.nextEntry ?: break
                names += entry.name
                zip.closeEntry()
            }
        }
        val prefix = sharedPrefix(names)
        val incoming = File(dest.parentFile, "firmware-incoming")
        incoming.deleteRecursively()
        incoming.mkdirs()
        var screams = 0
        var creaks = 0
        var sawBin = false
        var total = 0L
        var entries = 0
        try {
            ZipInputStream(zipFile.inputStream().buffered()).use { zip ->
                while (true) {
                    val entry = zip.nextEntry ?: break
                    entries++
                    if (entries > MAX_ENTRIES) {
                        throw ReleaseException(ReleaseException.Reason.TOO_BIG)
                    }
                    val rel = normalize(entry.name, prefix) ?: continue
                    if (entry.isDirectory) {
                        continue
                    }
                    val target = when {
                        rel == BIN_NAME -> {
                            sawBin = true
                            File(incoming, BIN_NAME)
                        }
                        isClip(rel, "screams") -> {
                            screams++
                            File(incoming, rel)
                        }
                        isClip(rel, "creaks") -> {
                            creaks++
                            File(incoming, rel)
                        }
                        else -> null
                    } ?: continue
                    if (target.parentFile != incoming && target.parentFile?.parentFile != incoming) {
                        continue
                    }
                    target.parentFile?.mkdirs()
                    FileOutputStream(target).use { out ->
                        val buf = ByteArray(8192)
                        while (true) {
                            val n = zip.read(buf)
                            if (n < 0) {
                                break
                            }
                            total += n
                            if (total > MAX_BYTES) {
                                throw ReleaseException(ReleaseException.Reason.TOO_BIG)
                            }
                            out.write(buf, 0, n)
                        }
                    }
                }
            }
        } catch (e: ReleaseException) {
            incoming.deleteRecursively()
            throw e
        }
        if (!sawBin) {
            incoming.deleteRecursively()
            throw ReleaseException(ReleaseException.Reason.NO_BIN)
        }
        dest.deleteRecursively()
        if (!incoming.renameTo(dest)) {
            incoming.copyRecursively(dest, overwrite = true)
            incoming.deleteRecursively()
        }
        return screams to creaks
    }

    private fun isClip(path: String, folder: String): Boolean {
        if (!path.startsWith("$folder/")) {
            return false
        }
        if (path.count { it == '/' } != 1) {
            return false
        }
        return path.endsWith(".wav", ignoreCase = true)
    }

    private fun sharedPrefix(rawNames: List<String>): String {
        val paths = rawNames.mapNotNull { normalize(it, "") }
        val tops = paths.map { it.substringBefore('/') }.toSet()
        if (tops.size != 1) {
            return ""
        }
        val top = tops.first()
        if (top == "screams" || top == "creaks" || top == BIN_NAME) {
            return ""
        }
        val nested = paths.any { it.startsWith("$top/") }
        val rootFile = paths.any { !it.contains('/') && it != top }
        if (nested && !rootFile) {
            return "$top/"
        }
        return ""
    }

    private fun normalize(raw: String, prefix: String): String? {
        val slash = raw.replace('\\', '/').trim('/')
        if (slash.startsWith("/") || slash.contains(':')) {
            return null
        }
        val parts = slash.split('/').filter { it.isNotEmpty() }
        if (parts.isEmpty() || parts.any { it == ".." || it == "." }) {
            return null
        }
        var path = parts.joinToString("/")
        if (path.startsWith("__MACOSX/") || path == "__MACOSX" || parts.any { it.startsWith("._") }) {
            return null
        }
        if (prefix.isNotEmpty()) {
            if (path == prefix.removeSuffix("/")) {
                return null
            }
            if (!path.startsWith(prefix)) {
                return null
            }
            path = path.removePrefix(prefix)
        }
        return path.ifEmpty { null }
    }

    private fun open(url: String, json: Boolean): HttpURLConnection {
        val conn = URL(url).openConnection() as HttpURLConnection
        conn.connectTimeout = 15_000
        conn.readTimeout = 30_000
        conn.instanceFollowRedirects = true
        conn.setRequestProperty("User-Agent", "CoffinCompanion")
        conn.setRequestProperty(
            "Accept",
            if (json) "application/vnd.github+json" else "application/octet-stream",
        )
        return conn
    }
}

sealed class PullProgress {
    data object Checking : PullProgress()
    data class Downloading(val bytes: Long, val total: Long) : PullProgress()
    data object Unpacking : PullProgress()
}

data class PullResult(val tag: String, val screams: Int, val creaks: Int)

class ReleaseException(val reason: Reason, message: String? = null) : Exception(message) {
    enum class Reason {
        NO_REPO,
        NO_RELEASE,
        NO_ZIP,
        NO_BIN,
        TOO_BIG,
        FAILED,
    }
}
