package cassia.app

import android.view.Surface
import cassia.app.store.Prefix
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext

class CassiaManager {
    companion object {
        init {
            System.loadLibrary("cassia")
        }
    }

    private external fun runServer(runtimePath: String, prefixPath: String)

    private external fun stopServer()

    external fun setSurface(surface: Surface?)

    private val mutex = Mutex()

    var runningPrefix: Prefix? = null
        private set

    var thread: Thread? = null
        private set

    suspend fun start(prefixUuid: String) {
        mutex.withLock {
            if (runningPrefix != null)
                throw IllegalStateException("A prefix is already running")
            val prefix = CassiaApplication.instance.prefixes.updateLinks(prefixUuid)
            runningPrefix = prefix

            thread = Thread {
                runServer(prefix.runtimePath.toString(), prefix.path.toString())
            }

            thread?.start()
        }
    }

    suspend fun stop() {
        mutex.withLock {
            if (runningPrefix == null)
                throw IllegalStateException("No prefix is running")
            runningPrefix = null

            withContext(Dispatchers.IO) {
                stopServer()
                thread?.join()
            }
        }
    }
}
