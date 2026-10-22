package com.coffin.companion

import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL

/** Latest release carries the firmware image [BIN_NAME] and the clip pack [PACK_NAME] as their own assets. */
object CoffinRelease {
    const val OWNER = "BlackHeart-TF"
    const val REPO = "esp32_motion_prop"
    const val BIN_NAME = "esp32s3_8m.bin"
    const val PACK_NAME = "preset_pack.zip"

    private const val MAX_BYTES = 16L * 1024 * 1024

    fun pull(filesDir: File, name: String, onProgress: (PullProgress) -> Unit): PullResult {
        if (OWNER.isBlank() || REPO.isBlank()) {
            throw ReleaseException(ReleaseException.Reason.NO_REPO)
        }
        onProgress(PullProgress.Checking)
        val base = "https://api.github.com/repos/$OWNER/$REPO/releases"
        val release = try {
            JSONObject(getJson("$base/latest"))
        } catch (e: ReleaseException) {
            if (e.reason != ReleaseException.Reason.NO_RELEASE) {
                throw e
            }
            newest(JSONArray(getJson("$base?per_page=10")))
        }
        val tag = release.optString("tag_name").ifBlank { "release" }
        val url = assetUrl(release, name) ?: throw ReleaseException(ReleaseException.Reason.NO_BIN, name)
        val dir = File(filesDir, "firmware").apply { mkdirs() }
        val part = File(dir, "$name.part")
        val bin = File(dir, name)
        try {
            download(url, name, part, onProgress)
            if (!part.renameTo(bin)) {
                part.copyTo(bin, overwrite = true)
            }
        } finally {
            part.delete()
        }
        return PullResult(tag, bin)
    }

    /** Fallback when there is no full release: newest first, pre-releases included, drafts skipped. */
    private fun newest(releases: JSONArray): JSONObject {
        for (i in 0 until releases.length()) {
            val release = releases.getJSONObject(i)
            if (!release.optBoolean("draft")) {
                return release
            }
        }
        throw ReleaseException(ReleaseException.Reason.NO_RELEASE)
    }

    private fun getJson(url: String): String {
        val conn = open(url, json = true)
        try {
            when (conn.responseCode) {
                404 -> throw ReleaseException(ReleaseException.Reason.NO_RELEASE)
                in 200..299 -> return conn.inputStream.bufferedReader().use { it.readText() }
                else -> throw ReleaseException(ReleaseException.Reason.FAILED, "HTTP ${conn.responseCode}")
            }
        } finally {
            conn.disconnect()
        }
    }

    private fun assetUrl(release: JSONObject, name: String): String? {
        val assets = release.optJSONArray("assets") ?: return null
        for (i in 0 until assets.length()) {
            val asset = assets.getJSONObject(i)
            if (asset.optString("name").equals(name, ignoreCase = true)) {
                val url = asset.optString("browser_download_url")
                if (url.isNotBlank()) {
                    return url
                }
            }
        }
        return null
    }

    private fun download(url: String, name: String, dest: File, onProgress: (PullProgress) -> Unit) {
        val conn = open(url, json = false)
        try {
            val code = conn.responseCode
            if (code == 404) {
                throw ReleaseException(ReleaseException.Reason.NO_BIN, name)
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
}

data class PullResult(val tag: String, val bin: File)

class ReleaseException(val reason: Reason, message: String? = null) : Exception(message) {
    enum class Reason {
        NO_REPO,
        NO_RELEASE,
        NO_BIN,
        TOO_BIG,
        FAILED,
    }
}
