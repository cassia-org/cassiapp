package cassia.app.activity

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import cassia.app.ui.theme.CassiaTheme
import kotlin.system.exitProcess

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            CassiaTheme {
                Surface(color = MaterialTheme.colorScheme.background) {
                    Column(modifier = Modifier.padding(16.dp).fillMaxSize()) {
                        Text("Cassia doesn't support launching applications from the frontend yet.", fontSize = MaterialTheme.typography.bodyMedium.fontSize)
                        Button(onClick = { exitProcess(0); }, modifier = Modifier.padding(top = 8.dp)) {
                            Text("Exit")
                        }
                    }
                }
            }
        }
    }
}
