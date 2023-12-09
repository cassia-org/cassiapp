package cassia.app.activity

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import cassia.app.CassiaApplication
import cassia.app.store.Prefix
import cassia.app.ui.theme.CassiaTheme
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            CassiaTheme {
                Surface(color = MaterialTheme.colorScheme.background) {
                    Column(modifier = Modifier.fillMaxSize(), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
                        var running by remember { mutableStateOf(false) }
                        var reset by remember { mutableStateOf(true) }

                        var prefix by remember { mutableStateOf<Prefix?>(null) }
                        var defaultRuntime by remember { mutableStateOf<String?>(null) }
                        Text(text = prefix?.uuid ?: "No prefix detected")
                        Text(text = prefix?.runtimeId ?: "No runtime detected")

                        Text(text = if (running) "Running" else "Stopped")

                        Button(onClick = {
                            if (running) {
                                MainScope().launch {
                                    CassiaApplication.instance.manager.stop()
                                    running = false
                                    reset = true
                                }
                            } else {
                                prefix?.let {
                                    MainScope().launch {
                                        CassiaApplication.instance.manager.start(it.uuid)
                                        running = true
                                    }
                                }
                            }
                        }, enabled = prefix != null) {
                            Text(text = if (running) "Stop" else "Start")
                        }
                        Button(onClick = {
                            defaultRuntime?.let {
                                MainScope().launch {
                                    prefix = CassiaApplication.instance.prefixes.create("Default", it)
                                }
                            }
                        }, enabled = !running && defaultRuntime != null) {
                            Text(text = "Create Prefix (Default Runtime: ${defaultRuntime ?: "None"})")
                        }
                        Button(enabled = !running && reset && prefix != null, onClick = {
                            prefix?.let {
                                MainScope().launch {
                                    prefix = CassiaApplication.instance.prefixes.reset(it.uuid)
                                    reset = false
                                }
                            }
                        }) {
                            Text(text = "Reset")
                        }

                        LaunchedEffect(Unit) {
                            defaultRuntime = CassiaApplication.instance.runtimes.getDefault()?.uuid
                            prefix = CassiaApplication.instance.prefixes.getDefault()
                        }
                    }
                }
            }
        }
    }
}
