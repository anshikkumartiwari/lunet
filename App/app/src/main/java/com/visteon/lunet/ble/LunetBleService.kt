package com.visteon.lunet.ble

import android.annotation.SuppressLint
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.os.Binder
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import com.visteon.lunet.MainActivity
import com.visteon.lunet.R
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * Foreground Service maintaining the high-priority BLE connection to
 * Visteon Lunet OS 3.0 cluster with persistent status notifications and
 * exponential backoff auto-reconnect.
 */
class LunetBleService : Service() {

    private val binder = LocalBinder()
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    private var reconnectJob: Job? = null

    lateinit var bleManager: LunetBleManager
        private set

    inner class LocalBinder : Binder() {
        fun getService(): LunetBleService = this@LunetBleService
    }

    override fun onCreate() {
        super.onCreate()
        bleManager = com.visteon.lunet.LunetApp.instance?.bleManager ?: LunetBleManager(applicationContext)
        createNotificationChannel()
        startForegroundServiceNotification("Visteon Lunet OS 3.0: Ready")

        // Observe connection state for notifications and auto-reconnect
        scope.launch {
            bleManager.connectionState.collectLatest { state ->
                when (state) {
                    BleConnectionState.CONNECTED -> {
                        cancelReconnect()
                        updateNotification("Visteon Lunet: Connected to Cluster")
                    }
                    BleConnectionState.CONNECTING -> {
                        updateNotification("Visteon Lunet: Connecting...")
                    }
                    BleConnectionState.SCANNING -> {
                        updateNotification("Visteon Lunet: Scanning for cluster...")
                    }
                    BleConnectionState.DISCONNECTED -> {
                        updateNotification("Visteon Lunet: Disconnected")
                        scheduleAutoReconnect()
                    }
                }
            }
        }
    }

    private fun scheduleAutoReconnect() {
        if (com.visteon.lunet.LunetApp.instance?.autoReconnectEnabled?.value == false) return
        reconnectJob?.cancel()
        reconnectJob = scope.launch {
            var attempt = 1
            while (isActive && bleManager.connectionState.value == BleConnectionState.DISCONNECTED) {
                if (com.visteon.lunet.LunetApp.instance?.autoReconnectEnabled?.value == false) break
                val backoffSec = (1 shl (attempt - 1)).coerceAtMost(16)
                delay(backoffSec * 1000L)
                if (bleManager.connectionState.value == BleConnectionState.DISCONNECTED) {
                    startForegroundServiceNotification("Scanning for Visteon-Lunet...")
                    bleManager.startScan()
                }
                attempt++
            }
        }
    }

    private fun cancelReconnect() {
        reconnectJob?.cancel()
        reconnectJob = null
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                getString(R.string.notification_channel_name),
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = getString(R.string.notification_channel_desc)
                setShowBadge(false)
            }
            val manager = getSystemService(NotificationManager::class.java)
            manager?.createNotificationChannel(channel)
        }
    }

    private fun buildNotification(statusText: String): Notification {
        val intent = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP
        }
        val pendingIntent = PendingIntent.getActivity(
            this,
            0,
            intent,
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Visteon Lunet Companion")
            .setContentText(statusText)
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .setCategory(NotificationCompat.CATEGORY_SERVICE)
            .build()
    }

    @SuppressLint("InlinedApi")
    private fun startForegroundServiceNotification(initialText: String) {
        try {
            val notification = buildNotification(initialText)
            val hasConnectPermission = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                ContextCompat.checkSelfPermission(
                    this,
                    android.Manifest.permission.BLUETOOTH_CONNECT
                ) == PackageManager.PERMISSION_GRANTED
            } else true

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && hasConnectPermission) {
                startForeground(
                    NOTIFICATION_ID,
                    notification,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
                )
            } else {
                startForeground(NOTIFICATION_ID, notification)
            }
        } catch (e: Exception) {
            android.util.Log.e("LunetBleService", "startForeground failed: ${e.message}")
        }
    }

    private fun updateNotification(text: String) {
        try {
            val manager = getSystemService(Context.NOTIFICATION_SERVICE) as? NotificationManager
            manager?.notify(NOTIFICATION_ID, buildNotification(text))
        } catch (e: Exception) {
            android.util.Log.w("LunetBleService", "updateNotification failed: ${e.message}")
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_CONNECT -> {
                startForegroundServiceNotification("Scanning for Visteon-Lunet...")
                bleManager.startScan()
            }
            ACTION_DISCONNECT -> {
                cancelReconnect()
                bleManager.disconnect()
            }
        }
        return START_STICKY
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onDestroy() {
        cancelReconnect()
        super.onDestroy()
    }

    companion object {
        const val CHANNEL_ID = "visteon_lunet_channel"
        const val NOTIFICATION_ID = 3001
        const val ACTION_CONNECT = "com.visteon.lunet.action.CONNECT"
        const val ACTION_DISCONNECT = "com.visteon.lunet.action.DISCONNECT"
    }
}
