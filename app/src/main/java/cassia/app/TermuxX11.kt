package cassia.app

import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import android.os.Build
import android.system.Os
import android.util.Log
import dalvik.system.PathClassLoader
import java.lang.reflect.InvocationTargetException
import kotlin.io.path.exists

object TermuxX11 {
    private const val TAG = "cassia.kt.TermuxX11"
    private const val TARGET_APP_ID = "com.termux.x11"
    private const val TARGET_CLASS_ID = "com.termux.x11.CmdEntryPoint"

    @JvmStatic
    fun main(args: Array<String>) {
        val xkbConfigRoot = CassiaApplication.instance.cassiaExt.path.resolve("share/X11/xkb")
        if (!xkbConfigRoot.exists())
            error("XKB_CONFIG_ROOT not found: $xkbConfigRoot")
        Os.setenv("XKB_CONFIG_ROOT", xkbConfigRoot.toString(), true)

        val tmpDir = CassiaApplication.instance.cacheDir.resolve("tmp/")
        tmpDir.deleteRecursively()
        tmpDir.mkdirs()
        Os.setenv("TMPDIR", tmpDir.toString(), true)

        try {
            val targetInfo: PackageInfo = CassiaApplication.instance.packageManager.getPackageInfo(TARGET_APP_ID, 0) ?: error("Termux:X11 not installed")
            Log.i(TAG, "Running ${targetInfo.applicationInfo.sourceDir}::$TARGET_CLASS_ID::main of $TARGET_APP_ID application")
            val targetClass = Class.forName(
                TARGET_CLASS_ID, true,
                PathClassLoader(targetInfo.applicationInfo.sourceDir, null, ClassLoader.getSystemClassLoader())
            )
            targetClass.getMethod("main", Array<String>::class.java).invoke(null, args as Any)
        } catch (e: AssertionError) {
            System.err.println(e.message)
        } catch (e: InvocationTargetException) {
            e.cause!!.printStackTrace(System.err)
        } catch (e: Throwable) {
            Log.e(TAG, "Termux:X11 error", e)
            e.printStackTrace(System.err)
        }
    }
}
