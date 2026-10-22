package com.coffin.companion

import android.content.ContentResolver
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.DocumentsContract.Document
import java.io.File
import java.io.IOException
import java.util.zip.ZipInputStream

/** Unpacks a zip into a document tree the user picked. Files already on the drive are left alone. */
object DriveCopy {
    data class Result(val copied: Int, val skipped: Int)

    fun unpack(resolver: ContentResolver, tree: Uri, zip: File, onFile: (String) -> Unit): Result {
        val dirs = HashMap<String, String>()
        dirs[""] = DocumentsContract.getTreeDocumentId(tree)
        var copied = 0
        var skipped = 0
        ZipInputStream(zip.inputStream().buffered()).use { input ->
            while (true) {
                val entry = input.nextEntry ?: break
                val parts = entry.name.replace('\\', '/').split('/').filter { it.isNotEmpty() }
                if (parts.isEmpty() || parts.any { it == ".." || it.startsWith(".") } || parts[0] == "__MACOSX") {
                    continue
                }
                if (entry.isDirectory) {
                    dirFor(resolver, tree, dirs, parts)
                    continue
                }
                val parent = dirFor(resolver, tree, dirs, parts.dropLast(1))
                val name = parts.last()
                if (child(resolver, tree, parent, name) != null) {
                    skipped++
                    continue
                }
                val path = parts.joinToString("/")
                onFile(path)
                val doc = DocumentsContract.createDocument(
                    resolver,
                    DocumentsContract.buildDocumentUriUsingTree(tree, parent),
                    "application/octet-stream",
                    name,
                ) ?: throw IOException("Could not create $path")
                val out = resolver.openOutputStream(doc) ?: throw IOException("Could not write $path")
                out.use { input.copyTo(it) }
                copied++
            }
        }
        return Result(copied, skipped)
    }

    private fun dirFor(
        resolver: ContentResolver,
        tree: Uri,
        dirs: HashMap<String, String>,
        parts: List<String>,
    ): String {
        var path = ""
        var id = dirs.getValue("")
        for (part in parts) {
            path = if (path.isEmpty()) part else "$path/$part"
            val parentId = id
            id = dirs.getOrPut(path) {
                val found = child(resolver, tree, parentId, part)
                when {
                    found == null -> {
                        val made = DocumentsContract.createDocument(
                            resolver,
                            DocumentsContract.buildDocumentUriUsingTree(tree, parentId),
                            Document.MIME_TYPE_DIR,
                            part,
                        ) ?: throw IOException("Could not create $path")
                        DocumentsContract.getDocumentId(made)
                    }
                    found.second == Document.MIME_TYPE_DIR -> found.first
                    else -> throw IOException("$path is a file on the drive")
                }
            }
        }
        return id
    }

    /** FAT names are case-insensitive, so match the same way. */
    private fun child(resolver: ContentResolver, tree: Uri, parentId: String, name: String): Pair<String, String>? {
        val uri = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId)
        val columns = arrayOf(Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE)
        resolver.query(uri, columns, null, null, null)?.use { cursor ->
            while (cursor.moveToNext()) {
                if (cursor.getString(1).equals(name, ignoreCase = true)) {
                    return cursor.getString(0) to cursor.getString(2)
                }
            }
        }
        return null
    }
}
