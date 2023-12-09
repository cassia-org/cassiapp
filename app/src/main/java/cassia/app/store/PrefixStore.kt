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
import kotlin.io.path.createDirectory
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
    }

    private var prefixes = mutableListOf<Prefix>()
    private var scanned = false
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
                        } else if (!Files.exists(it.resolve("metadata.json"))) {
                            Log.w(TAG, "Prefix directory ${it.name} does not contain metadata.json")
                            false
                        } else {
                            true
                        }
                    }
                    .map {
                        val prefix = runCatching {
                            val json = Files.newInputStream(it.resolve("metadata.json")!!).use { stream -> stream.reader().readText() }
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

    private fun unlinkPrefix(prefix: Prefix): Prefix {
        for (link in prefix.runtimeLinks) {
            val path = prefix.path.resolve("pfx").resolve(link)
            if (path.exists() && !path.isSymbolicLink())
                Log.w(TAG, "Link $link for prefix ${prefix.name} (${prefix.uuid}) is not a symbolic link")
            path.deleteIfExists()
        }
        return prefix.copy(runtimeLinks = emptyList())
    }

    private suspend fun linkPrefix(prefix: Prefix): Prefix {
        return withContext(Dispatchers.IO) {
            val runtime = prefix.runtime()
            val links = mutableListOf<String>()
            for ((target, link) in runtime.prefixLinks) {
                val linkPath = prefix.path.resolve("pfx").resolve(link)
                if (linkPath.exists()) {
                    Log.w(TAG, "Link $linkPath for prefix ${prefix.name} (${prefix.uuid}) already exists")
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

    private suspend fun writePrefix(prefix: Prefix) {
        withContext(Dispatchers.IO) {
            Files.newOutputStream(prefix.path.resolve("metadata.json")).use { stream ->
                Log.w(TAG, "Writing metadata.json for ${prefix.name} (${prefix.uuid}): ${Json.encodeToString(Prefix.serializer(), prefix)}")
                stream.writer().use {
                    it.write(Json.encodeToString(Prefix.serializer(), prefix))
                }
            }
        }
    }

    suspend fun create(name: String, runtimeId: String): Prefix {
        return withContext(Dispatchers.IO) {
            mutex.withLock {
                var uuid: String
                var prefixPath: Path
                do {
                    uuid = java.util.UUID.randomUUID().toString()
                    prefixPath = Paths.get(path.toString(), uuid)
                } while (Files.exists(prefixPath))

                Files.createDirectory(prefixPath)
                Files.createDirectory(prefixPath.resolve("pfx"))
                Files.createDirectory(prefixPath.resolve("home"))

                val prefix = linkPrefix(Prefix(uuid, name, runtimeId))
                writePrefix(prefix)
                prefixes.add(prefix)
                prefix
            }
        }
    }

    suspend fun list(): List<Prefix> {
        if (!scanned)
            return scan()
        mutex.withLock {
            return prefixes
        }
    }

    suspend fun get(uuid: String): Prefix? = list().find { it.uuid == uuid }

    suspend fun getDefault(): Prefix? {
        val prefixes = list()
        // TODO: Use a setting for the default prefix.
        return prefixes.firstOrNull()
    }

    suspend fun update(uuid: String, func: suspend (Prefix) -> Prefix): Prefix {
        return withContext(Dispatchers.IO) {
            mutex.withLock {
                val prefixIndex = prefixes.indexOfFirst { it.uuid == uuid }
                if (prefixIndex == -1)
                    throw IllegalArgumentException("Prefix $prefixIndex is not in the store")

                val updatedPrefix = func(prefixes[prefixIndex])
                writePrefix(updatedPrefix)
                prefixes[prefixIndex] = updatedPrefix
                updatedPrefix
            }
        }
    }

    suspend fun updateLinks(uuid: String): Prefix {
        return update(uuid) { prefix ->
            unlinkPrefix(prefix)
            linkPrefix(prefix)
        }
    }

    suspend fun updateRuntime(uuid: String, runtimeId: String): Prefix {
        return update(uuid) { prefix ->
            unlinkPrefix(prefix)
            linkPrefix(prefix.copy(runtimeId = runtimeId))
        }
    }

    @OptIn(ExperimentalPathApi::class)
    suspend fun reset(uuid: String): Prefix {
        return update(uuid) { prefix ->
            val pfxPath = prefix.path.resolve("pfx")
            pfxPath.deleteRecursively()
            pfxPath.createDirectory()

            val homePath = prefix.path.resolve("home")
            homePath.deleteRecursively()
            homePath.createDirectory()

            linkPrefix(prefix.copy(runtimeLinks = emptyList()))
        }
    }

    suspend fun delete(uuid: String) {
        withContext(Dispatchers.IO) {
            mutex.withLock {
                val prefix = prefixes.find { it.uuid == uuid } ?: return@withContext
                Files.walk(prefix.path).sorted(Comparator.reverseOrder()).forEach(Files::delete)
                prefixes.remove(prefix)
            }
        }
    }
}
