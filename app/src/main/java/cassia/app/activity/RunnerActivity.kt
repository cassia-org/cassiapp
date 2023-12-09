package cassia.app.activity

import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.WindowInsetsController
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import cassia.app.R
import cassia.app.ui.theme.CassiaTheme
import kotlin.math.abs

class RunnerActivity : ComponentActivity() {
    /**
     * Forces a 60Hz refresh rate for the primary display when [enable] is true, otherwise selects the highest available refresh rate
     */
    private fun force60HzRefreshRate(enable : Boolean) {
        // Hack for MIUI devices since they don't support the standard Android APIs
        try {
            val setFpsIntent = Intent("com.miui.powerkeeper.SET_ACTIVITY_FPS")
            setFpsIntent.putExtra("package_name", "skyline.emu")
            setFpsIntent.putExtra("isEnter", enable)
            sendBroadcast(setFpsIntent)
        } catch (_ : Exception) {
        }

        @Suppress("DEPRECATION") val display = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) display!! else windowManager.defaultDisplay
        if (enable)
            display?.supportedModes?.minByOrNull { abs(it.refreshRate - 60f) }?.let { window.attributes.preferredDisplayModeId = it.modeId }
        else
            display?.supportedModes?.maxByOrNull { it.refreshRate }?.let { window.attributes.preferredDisplayModeId = it.modeId }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setContent {
            CassiaTheme {
                Surface(color = MaterialTheme.colorScheme.background) {
                    var status by remember { mutableStateOf("Waiting") }
                    SurfaceViewComposable(
                        surfaceCreatedHandler = { holder -> status = "Surface created"; },
                        surfaceChangedHandler = { holder, format, width, height ->
                            status = "Surface active (${width}x${height})"
                        },
                        surfaceDestroyedHandler = { holder ->
                            status = "Surface destroyed"
                        }
                    )
                    Column(modifier = Modifier.fillMaxSize(), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
                        Text("Cassia Runner", style = MaterialTheme.typography.bodyMedium, color = Color.White)
                        Text("Status: $status", style = MaterialTheme.typography.bodyMedium, color = Color.White)
                    }
                    Column(modifier = Modifier.fillMaxSize(), horizontalAlignment = Alignment.End, verticalArrangement = Arrangement.Bottom) {
                        Icon(painterResource(R.drawable.cassia_mono), contentDescription = "Cassia Logo", modifier = Modifier.padding(16.dp), tint = Color.White.copy(alpha = 0.5f))
                    }
                }
            }
        }

        window.attributes.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            // Android might not allow child views to overlap the system bars
            // Override this behavior and force content to extend into the cutout area
            window.setDecorFitsSystemWindows(false)

            window.insetsController?.let {
                it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                it.hide(android.view.WindowInsets.Type.systemBars())
            }
        }

        force60HzRefreshRate(false)
    }

    override fun onResume() {
        super.onResume()

        if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.R) {
            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    or View.SYSTEM_UI_FLAG_FULLSCREEN)
        }

        force60HzRefreshRate(false)
    }

    override fun onDestroy() {
        super.onDestroy()
        Log.e("cassia", "onDestroy")
    }
}

@Composable
fun SurfaceViewComposable(surfaceCreatedHandler: (SurfaceHolder) -> Unit, surfaceChangedHandler: (SurfaceHolder, Int, Int, Int) -> Unit, surfaceDestroyedHandler: (SurfaceHolder) -> Unit) {
    AndroidView(
        modifier = Modifier.fillMaxSize(),
        factory = { context ->
            SurfaceView(context).apply {
                holder.addCallback(object : SurfaceHolder.Callback {
                    override fun surfaceCreated(holder: SurfaceHolder) {
                        surfaceCreatedHandler(holder)
                    }

                    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
                        surfaceChangedHandler(holder, format, width, height)
                    }

                    override fun surfaceDestroyed(holder: SurfaceHolder) {
                        surfaceDestroyedHandler(holder)
                    }
                })
            }
        }
    )
}
