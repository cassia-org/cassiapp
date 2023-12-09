package cassia.app.store

import android.util.Log
import cassia.app.CassiaApplication
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.delay
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import java.nio.file.Files
import java.nio.file.Paths
import kotlin.io.path.name
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import java.nio.file.Path

@Serializable
data class Runtime(val uuid: String, val name: String, val version: Int, val author: String, val description: String, val prefixLinks: Map<String, String>) {
    val id: String
        get() = "${uuid}v${version}"

    val path: Path
        get() = CassiaApplication.instance.runtimes.path.resolve(id)
}

class RuntimeStore(val path: Path) {
    companion object {
        const val TAG = "cassia.kt.RuntimeStore"

        /**
         * @brief A custom UUID that will override the default runtime for development purposes.
         * @note No runtime with this UUID can be installed, it needs to be placed in the runtimes directory manually.
         */
        const val OVERRIDE_UUID = "00000000-0000-0000-0000-000000000000"
    }

    private var runtimes = mutableListOf<Runtime>()
    private var scanned = false
    private val mutex = Mutex()

    init {
        if (!Files.exists(path)) {
            Log.d(TAG, "Initializing runtime store at $path")
            Files.createDirectory(path)
        }
    }

    suspend fun scan(): List<Runtime> {
        mutex.withLock {
            runtimes = withContext(Dispatchers.IO) {
                Files.list(path)
                    .filter {
                        if (!Files.isDirectory(it)) {
                            Log.w(TAG, "Unexpected file in runtime directory: ${it.name}")
                            false
                        } else if (!Files.exists(it.resolve("metadata.json"))) {
                            Log.w(TAG, "Runtime directory ${it.name} does not contain metadata.json")
                            false
                        } else {
                            true
                        }
                    }
                    .map {
                        val runtime = runCatching {
                            val json = Files.newInputStream(it.resolve("metadata.json")!!).use { stream -> stream.reader().readText() }
                            Json.decodeFromString(Runtime.serializer(), json)
                        }.getOrElse { e ->
                            Log.w(TAG, "Failed to parse metadata.json for ${it.name}: $e")
                            return@map null
                        }

                        if (runtime.id == it.name) {
                            Log.d(TAG, "Found runtime ${runtime.name} v${runtime.version} by ${runtime.author}")
                            runtime
                        } else {
                            Log.w(TAG, "Runtime ID ${runtime.id} does not match directory name ${it.name}")
                            null
                        }
                    }
                    .iterator().asSequence().filterNotNull().toMutableList()
            }

            scanned = true
            return runtimes
        }
    }

    suspend fun list(): List<Runtime> {
        if (!scanned)
            return scan()
        mutex.withLock {
            return runtimes
        }
    }

    suspend fun get(id: String): Runtime? {
        return list().find { it.id == id }
    }

    suspend fun getDefault(): Runtime? {
        val runtimes = list()
        // TODO: Use a setting for the default runtime.
        return runtimes.find { it.uuid == OVERRIDE_UUID } ?: runtimes.firstOrNull()
    }
}
