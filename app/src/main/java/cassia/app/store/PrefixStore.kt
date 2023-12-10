package cassia.app.store

import android.util.Log
import cassia.app.CassiaApplication
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.Paths
import kotlin.io.path.ExperimentalPathApi
import kotlin.io.path.deleteExisting
import kotlin.io.path.deleteIfExists
import kotlin.io.path.deleteRecursively
import kotlin.io.path.exists
import kotlin.io.path.isSymbolicLink
import kotlin.io.path.name

@Serializable
data class Prefix(val uuid: String, val name: String, val runtimeId: String, val runtimeLinks: List<String> = emptyList()) {
    val path: Path
        get() = CassiaApplication.instance.prefixes.path.resolve(uuid)
    val runtimePath: Path
        get() = CassiaApplication.instance.runtimes.path.resolve(runtimeId)

    suspend fun runtime(): Runtime {
        return CassiaApplication.instance.runtimes.get(runtimeId) ?: throw IllegalStateException("Runtime $runtimeId not found")
    }
}

class PrefixStore(val path: Path) {
    companion object {
        const val TAG = "cassia.kt.PrefixStore"
        const val PREFIX_SUBDIR = "pfx"
        const val HOME_SUBDIR = "home"
        const val METADATA_FILE = "metadata.json"
    }

    private var scanned = false
    private var prefixes = mutableListOf<Prefix>()
        get() = if (scanned) field else throw IllegalStateException("Prefix store has not been scanned yet")
    private val mutex = Mutex()

    init {
        if (!Files.exists(path)) {
            Log.d(TAG, "Initializing prefix store at $path")
            Files.createDirectory(path)
        }
    }

    suspend fun scan(): List<Prefix> {
        mutex.withLock {
            prefixes = withContext(Dispatchers.IO) {
                Files.list(path)
                    .filter {
                        if (!Files.isDirectory(it)) {
                            Log.w(TAG, "Unexpected file in prefix directory: ${it.name}")
                            false
                        } else if (!Files.exists(it.resolve(METADATA_FILE))) {
                            Log.w(TAG, "Prefix directory ${it.name} does not contain metadata.json")
                            false
                        } else {
                            true
                        }
                    }
                    .map {
                        val prefix = runCatching {
                            val json = Files.newInputStream(it.resolve(METADATA_FILE)!!).use { stream -> stream.reader().readText() }
                            Json.decodeFromString(Prefix.serializer(), json)
                        }.getOrElse { e ->
                            Log.w(TAG, "Failed to parse metadata.json for ${it.name}: $e")
                            return@map null
                        }

                        if (prefix.uuid == it.name) {
                            Log.d(TAG, "Found prefix ${prefix.name} (${prefix.uuid})")
                            prefix
                        } else {
                            Log.w(TAG, "Prefix ID ${prefix.uuid} does not match directory name ${it.name}")
                            null
                        }
                    }
                    .iterator().asSequence().filterNotNull().toMutableList()
            }

            scanned = true
            return prefixes
        }
    }

    private suspend fun unlinkPrefixRuntimeLocked(prefix: Prefix): Prefix {
        return withContext(Dispatchers.IO) {
            for (link in prefix.runtimeLinks) {
                val path = prefix.path.resolve(PREFIX_SUBDIR).resolve(link)
                if (path.exists() && !path.isSymbolicLink())
                    Log.w(TAG, "Link $link for prefix ${prefix.name} (${prefix.uuid}) is not a symbolic link")
                path.deleteIfExists()
            }
            prefix.copy(runtimeLinks = emptyList())
        }
    }

    private suspend fun linkPrefixRuntimeLocked(prefix: Prefix): Prefix {
        return withContext(Dispatchers.IO) {
            val runtime = prefix.runtime()
            val links = mutableListOf<String>()
            for ((target, link) in runtime.prefixLinks) {
                val linkPath = prefix.path.resolve(PREFIX_SUBDIR).resolve(link)
                if (linkPath.exists()) {
                    Log.d(TAG, "Link $linkPath for prefix ${prefix.name} (${prefix.uuid}) already exists")
                    linkPath.deleteExisting()
                }
                Files.createDirectories(linkPath.parent)
                val targetPath = prefix.runtimePath.resolve(target)
                Files.createSymbolicLink(linkPath, targetPath)
                links.add(link)
            }
            prefix.copy(runtimeLinks = links)
        }
    }

    private suspend fun writePrefixLocked(prefix: Prefix) {
        withContext(Dispatchers.IO) {
            Files.newOutputStream(prefix.path.resolve(METADATA_FILE)).use { stream ->
                Log.w(TAG, "Writing $METADATA_FILE for ${prefix.name} (${prefix.uuid}): ${Json.encodeToString(Prefix.serializer(), prefix)}")
                stream.writer().use {
                    it.write(Json.encodeToString(Prefix.serializer(), prefix))
                }
            }
        }
    }

    private suspend fun createLocked(name: String, runtimeId: String): Prefix {
        return withContext(Dispatchers.IO) {
            var uuid: String
            var prefixPath: Path
            do {
                uuid = java.util.UUID.randomUUID().toString()
                prefixPath = Paths.get(path.toString(), uuid)
            } while (Files.exists(prefixPath))

            Files.createDirectory(prefixPath)
            Files.createDirectory(prefixPath.resolve(PREFIX_SUBDIR))
            Files.createDirectory(prefixPath.resolve(HOME_SUBDIR))

            val prefix = linkPrefixRuntimeLocked(Prefix(uuid, name, runtimeId))
            writePrefixLocked(prefix)
            prefixes.add(prefix)
            prefix
        }
    }

    suspend fun create(name: String, runtimeId: String): Prefix = mutex.withLock { createLocked(name, runtimeId) }

    @OptIn(ExperimentalPathApi::class)
    private suspend fun deleteLocked(uuid: String) {
        withContext(Dispatchers.IO) {
            val prefix = prefixes.find { it.uuid == uuid } ?: return@withContext
            prefix.path.deleteRecursively()
            prefixes.remove(prefix)
        }
    }

    suspend fun delete(uuid: String) = mutex.withLock { deleteLocked(uuid) }

    suspend fun reset(uuid: String): Prefix {
        return mutex.withLock {
            val prefix = prefixes.find { it.uuid == uuid } ?: throw IllegalArgumentException("Prefix $uuid is not in the store")
            deleteLocked(uuid)
            createLocked(prefix.name, prefix.runtimeId)
        }
    }

    suspend fun get(uuid: String): Prefix? = mutex.withLock { prefixes.find { it.uuid == uuid } }

    suspend fun getDefault(): Prefix? {
        return mutex.withLock {
            // TODO: Use a setting for the default prefix.
            prefixes.firstOrNull()
        }
    }

    suspend fun update(uuid: String, func: suspend (Prefix) -> Prefix): Prefix {
        return mutex.withLock {
            val prefixIndex = prefixes.indexOfFirst { it.uuid == uuid }
            if (prefixIndex == -1)
                throw IllegalArgumentException("Prefix $prefixIndex is not in the store")

            val updatedPrefix = func(prefixes[prefixIndex])
            writePrefixLocked(updatedPrefix)
            prefixes[prefixIndex] = updatedPrefix
            updatedPrefix
        }
    }

    suspend fun updateLinks(uuid: String): Prefix {
        return update(uuid) { prefix ->
            unlinkPrefixRuntimeLocked(prefix)
            linkPrefixRuntimeLocked(prefix)
        }
    }

    suspend fun updateRuntime(uuid: String, runtimeId: String): Prefix {
        return update(uuid) { prefix ->
            unlinkPrefixRuntimeLocked(prefix)
            linkPrefixRuntimeLocked(prefix.copy(runtimeId = runtimeId))
        }
    }
}
