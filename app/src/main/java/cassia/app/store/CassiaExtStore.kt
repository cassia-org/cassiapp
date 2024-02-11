package cassia.app.store

import android.util.Log
import cassia.app.CassiaApplication
import org.apache.commons.compress.archivers.tar.TarArchiveInputStream
import org.apache.commons.compress.compressors.gzip.GzipCompressorInputStream
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.Paths
import kotlin.io.path.ExperimentalPathApi
import kotlin.io.path.deleteIfExists
import kotlin.io.path.deleteRecursively
import kotlin.io.path.exists

/**
 * Manages extracting cassiaext from the APK asset into the data directory and keeping it up to date.
 */
@OptIn(ExperimentalPathApi::class)
class CassiaExtStore(val path: Path) {
    companion object {
        const val TAG = "cassia.kt.CassiaExtStore"
        const val ID_FILE = "cassiaext.id"
        const val TAR_FILE = "cassiaext.tar" // Note: Android asset packing recompresses tar.gz files to tar.
        const val README_FILE = "README.txt"
    }

    init {
        val assets = CassiaApplication.instance.assets

        val id = assets.open(ID_FILE).use { it.reader().readText().trim() }
        var existingId: String? = null
        if (path.exists())
            existingId = path.resolve(ID_FILE).toFile().takeIf { it.exists() }?.readText()?.trim()

        if (id != existingId) {
            Log.i(TAG, "Extracting cassiaext to '$path' ($id != $existingId)")

            path.deleteRecursively()
            Files.createDirectories(path)

            assets.open(TAR_FILE).use { input ->
                TarArchiveInputStream(input).use { tarInput ->
                    while (true) {
                        val entry = tarInput.nextTarEntry ?: break
                        val entryPath = path.resolve(entry.name)
                        if (entry.isSymbolicLink) {
                            val target = Paths.get(entry.linkName)
                            entryPath.deleteIfExists()
                            Files.createSymbolicLink(entryPath, target)
                        } else if (entry.isFile) {
                            entryPath.parent?.let { Files.createDirectories(it) }
                            val entryFile = entryPath.toFile()
                            entryFile.outputStream().use { output ->
                                tarInput.copyTo(output)
                            }
                            entryFile.setExecutable(entry.mode and 0b111 != 0, false)
                        }
                    }
                }
            }

            Files.write(path.resolve(ID_FILE), id.toByteArray())

            Files.write(
                path.resolve(README_FILE), """
            This directory contains Cassia's external dependencies, do NOT touch this unless you know what you're doing.
            It is managed by the Cassia app, any modifications will be wiped out when a new APK is installed.
            """.trimIndent().toByteArray()
            )
        }
    }
}
