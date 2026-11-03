package cc700.app.ui

import android.Manifest
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import cc700.app.data.ConnectionMode

private val TABS = listOf("Unit", "Workouts", "Routes", "Rides", "Profile", "Settings")

class MainActivity : ComponentActivity() {
    private val model: AppModel by viewModels()
    private val tabRequest = mutableIntStateOf(-1)

    private val permissions = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { granted ->
        if (granted.values.any { !it }) model.say("Bluetooth permission is needed to reach the unit.")
        else model.connect()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        handle(intent)
        setContent {
            CcTheme {
                var tab by rememberSaveable { mutableIntStateOf(0) }
                val req by tabRequest
                LaunchedEffect(req) { if (req >= 0) { tab = req; tabRequest.intValue = -1 } }
                val snack = remember { SnackbarHostState() }
                val msg by model.message.collectAsState()
                val busy by model.busy.collectAsState()
                LaunchedEffect(msg) { msg?.let { snack.showSnackbar(it); model.messageShown() } }
                Scaffold(
                    snackbarHost = { SnackbarHost(snack) },
                    bottomBar = {
                        NavigationBar {
                            TABS.forEachIndexed { i, name ->
                                NavigationBarItem(selected = tab == i, onClick = { tab = i }, icon = {},
                                    label = { Text(name, fontSize = 11.sp, maxLines = 1) })
                            }
                        }
                    },
                ) { pad ->
                    Column(Modifier.fillMaxSize().padding(pad)) {
                        busy?.let {
                            LinearProgressIndicator(Modifier.fillMaxWidth())
                            Text("$it…", fontSize = 12.sp, color = Tok.muted, modifier = Modifier.padding(start = 16.dp, top = 4.dp))
                        }
                        Box(Modifier.fillMaxSize()) {
                            when (tab) {
                                0 -> UnitScreen(model)
                                1 -> WorkoutsScreen(model)
                                2 -> RoutesScreen(model)
                                3 -> RidesScreen(model)
                                4 -> ProfileScreen(model)
                                else -> SettingsScreen(model)
                            }
                        }
                    }
                }
            }
        }
    }

    override fun onStart() {
        super.onStart()
        // Reconnect to the bonded unit whenever the app comes to the front. Over BLE the permissions come
        // first (granted ones return at once); they are also what Add unit's scan needs.
        if (model.settings.value.mode == ConnectionMode.SIMULATED) model.connect() else askBle()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handle(intent)
    }

    private fun askBle() {
        val need = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        else arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        permissions.launch(need)
    }

    /** Share target: text is a workout; a shared file is a workout (JSON) or a route (GPX). */
    private fun handle(intent: Intent?) {
        if (intent?.action != Intent.ACTION_SEND) return
        val text = intent.getStringExtra(Intent.EXTRA_TEXT)
        @Suppress("DEPRECATION")
        val uri: Uri? = if (Build.VERSION.SDK_INT >= 33) intent.getParcelableExtra(Intent.EXTRA_STREAM, Uri::class.java)
        else intent.getParcelableExtra(Intent.EXTRA_STREAM)
        when {
            text != null -> { model.incomingWorkout.value = text; tabRequest.intValue = 1 }
            uri != null -> {
                val isGpx = intent.type?.contains("gpx") == true || uri.lastPathSegment?.endsWith(".gpx", true) == true
                if (isGpx) {
                    model.importGpx(uri, uri.lastPathSegment?.substringAfterLast('/')?.substringBeforeLast('.') ?: "Route")
                    tabRequest.intValue = 2
                } else {
                    model.incomingWorkout.value = contentResolver.openInputStream(uri)?.use { it.readBytes().decodeToString() }
                    tabRequest.intValue = 1
                }
            }
        }
    }
}
