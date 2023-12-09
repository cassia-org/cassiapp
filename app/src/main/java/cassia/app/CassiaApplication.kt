package cassia.app

import android.app.Application

class CassiaApplication : Application() {
    companion object {
        lateinit var instance: CassiaApplication
            private set
    }

    init {
        instance = this
    }

    override fun onCreate() {
        super.onCreate()
        instance = this
    }
}
