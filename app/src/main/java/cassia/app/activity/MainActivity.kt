package cassia.app.activity

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
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
import androidx.compose.ui.unit.dp
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
                    Column(modifier = Modifier.fillMaxSize(), horizontalAlignment = Alignment.Start, verticalArrangement = Arrangement.Center) {
                        var starting by remember { mutableStateOf(false) }
                        var stopping by remember { mutableStateOf(false) }
                        var running by remember { mutableStateOf(false) }
                        var reset by remember { mutableStateOf(true) }
                        var looping by remember { mutableStateOf(false) }

                        var prefix by remember { mutableStateOf<Prefix?>(null) }
                        var defaultRuntime by remember { mutableStateOf<String?>(null) }
                        Text(text = "Prefix: ${prefix?.uuid ?: "No prefix detected"}")
                        Text(text = "Runtime: ${prefix?.runtimeId ?: "No runtime detected"}")

                        var counter by remember { mutableStateOf(0) }
                        Text(text = "Run Counter: $counter")

                        Text(text = "Status: " + if (running) "Running" else if (starting) "Started" else if (stopping) "Stopping" else "Stopped")

                        Row {
                            Button(onClick = {
                                if (running) {
                                    MainScope().launch {
                                        stopping = true
                                        CassiaApplication.instance.manager.stop()
                                        stopping = false
                                        running = false
                                        reset = true
                                    }
                                } else {
                                    prefix?.let {
                                        MainScope().launch {
                                            starting = true
                                            CassiaApplication.instance.manager.start(it.uuid)
                                            running = true
                                            starting = false
                                            counter++
                                        }
                                    }
                                }
                            }, enabled = prefix != null && !starting && !stopping && !looping, modifier = Modifier.padding(horizontal = 8.dp)) {
                                Text(text = if (running) "Stop" else "Start")
                            }
                            Button(onClick = {
                                prefix?.let {
                                    looping = !looping
                                    MainScope().launch {
                                        while (looping) {
                                            starting = true
                                            CassiaApplication.instance.manager.start(it.uuid)
                                            running = true
                                            starting = false
                                            stopping = true
                                            CassiaApplication.instance.manager.stop()
                                            stopping = false
                                            running = false
                                            reset = true
                                            counter++
                                        }
                                    }
                                }
                            }, enabled = prefix != null && (looping || !starting && !running && !stopping)) {
                                Text(text = if (looping) "Stop Loop" else "Start Loop")
                            }
                        }
                        Row {
                            Button(onClick = {
                                defaultRuntime?.let {
                                    MainScope().launch {
                                        prefix = CassiaApplication.instance.prefixes.create("Default", it)
                                    }
                                }
                            }, enabled = !running && defaultRuntime != null, modifier = Modifier.padding(horizontal = 8.dp)) {
                                Text(text = "Create Prefix (Default)")
                            }
                            Button(enabled = !running && reset && prefix != null, onClick = {
                                prefix?.let {
                                    MainScope().launch {
                                        prefix = CassiaApplication.instance.prefixes.reset(it.uuid)
                                        reset = false
                                    }
                                }
                            }) {
                                Text(text = "Reset Prefix")
                            }
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
