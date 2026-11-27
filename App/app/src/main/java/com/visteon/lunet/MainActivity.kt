package com.visteon.lunet

import android.Manifest
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import com.visteon.lunet.ui.LunetMainScreen
import com.visteon.lunet.ui.theme.LunetAppTheme
import com.visteon.lunet.viewmodel.LunetViewModel

class MainActivity : ComponentActivity() {

    private val viewModel: LunetViewModel by viewModels()

    private val requestPermissionsLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { permissions ->
        val app = application as? LunetApp
        app?.telephonyBridge?.registerCallListener()

        val bleScanGranted = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            permissions[Manifest.permission.BLUETOOTH_SCAN] == true
        } else true

        val bleConnectGranted = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            permissions[Manifest.permission.BLUETOOTH_CONNECT] == true
        } else true

        if (bleScanGranted && bleConnectGranted) {
            viewModel.connect()
            app?.carKeyManager?.autoConnectIfSaved()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        requestRequiredPermissions()

        setContent {
            val isDark by viewModel.isDarkMode.collectAsState()
            LunetAppTheme(isDark = isDark) {
                LunetMainScreen(viewModel = viewModel)
            }
        }
    }

    override fun onResume() {
        super.onResume()
        (application as? LunetApp)?.telephonyBridge?.registerCallListener()
    }

    private fun requestRequiredPermissions() {
        val permissions = mutableListOf<String>()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            permissions.add(Manifest.permission.BLUETOOTH_SCAN)
            permissions.add(Manifest.permission.BLUETOOTH_CONNECT)
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            permissions.add(Manifest.permission.POST_NOTIFICATIONS)
        }

        // Phone call handling permissions
        permissions.add(Manifest.permission.READ_PHONE_STATE)
        permissions.add(Manifest.permission.READ_CALL_LOG)
        permissions.add(Manifest.permission.READ_CONTACTS)
        permissions.add(Manifest.permission.ANSWER_PHONE_CALLS)

        // Weather sync location permissions (ACCESS_COARSE_LOCATION & ACCESS_FINE_LOCATION)
        permissions.add(Manifest.permission.ACCESS_COARSE_LOCATION)
        permissions.add(Manifest.permission.ACCESS_FINE_LOCATION)

        requestPermissionsLauncher.launch(permissions.toTypedArray())
    }
}
