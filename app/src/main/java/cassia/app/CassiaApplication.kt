package cassia.app

import android.app.Application
import cassia.app.store.PrefixStore
import cassia.app.store.RuntimeStore
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.launch
import java.nio.file.Paths

class CassiaApplication : Application() {
    companion object {
        lateinit var instance: CassiaApplication
            private set
    }

    init {
        instance = this
    }

    lateinit var runtimes: RuntimeStore
        private set
    lateinit var prefixes: PrefixStore
        private set

    override fun onCreate() {
        super.onCreate()
        instance = this

        runtimes = RuntimeStore(Paths.get(filesDir.absolutePath, "runtimes"))
        prefixes = PrefixStore(Paths.get(filesDir.absolutePath, "prefixes"))
        MainScope().launch {
            runtimes.scan()
            prefixes.scan()
        }
    }
}
