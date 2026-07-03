package com.saboteur.shintaioperator

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.runtime.LaunchedEffect

class MainActivity : ComponentActivity() {

    private val vm: OperatorViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            // The Operator needs BLUETOOTH_SCAN (to find the board) and
            // BLUETOOTH_CONNECT (to talk to it). Request both; the view model
            // decides what it can do with whatever was granted.
            val launcher = rememberLauncherForActivityResult(
                ActivityResultContracts.RequestMultiplePermissions()
            ) { grants ->
                vm.onPermissions(
                    hasScan = grants[Manifest.permission.BLUETOOTH_SCAN] ?: hasPerm(SCAN),
                    hasConnect = grants[Manifest.permission.BLUETOOTH_CONNECT] ?: hasPerm(CONNECT),
                )
            }

            val request = {
                if (hasPerm(SCAN) && hasPerm(CONNECT)) {
                    vm.onPermissions(hasScan = true, hasConnect = true)
                } else {
                    launcher.launch(arrayOf(SCAN, CONNECT))
                }
            }

            LaunchedEffect(Unit) { request() }
            OperatorScreen(vm, onRequestPermissions = request)
        }
    }

    private fun hasPerm(p: String) =
        checkSelfPermission(p) == PackageManager.PERMISSION_GRANTED

    private companion object {
        const val SCAN = Manifest.permission.BLUETOOTH_SCAN
        const val CONNECT = Manifest.permission.BLUETOOTH_CONNECT
    }
}
