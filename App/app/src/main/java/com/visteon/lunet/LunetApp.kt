package com.visteon.lunet

import android.app.Application
import android.content.Intent
import android.os.Build
import android.util.Log
import com.visteon.lunet.ble.BleConnectionState
import com.visteon.lunet.ble.ClusterTelemetry
import com.visteon.lunet.ble.DriveMode
import com.visteon.lunet.ble.Gear
import com.visteon.lunet.ble.LunetBleManager
import com.visteon.lunet.ble.LunetBleService
import com.visteon.lunet.ble.NavDirection
import com.visteon.lunet.bridge.MediaBridgeManager
import com.visteon.lunet.bridge.PhoneFinderManager
import com.visteon.lunet.bridge.SyncEngine
import com.visteon.lunet.bridge.TelephonyBridgeManager
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

class LunetApp : Application() {

    private val tag = "LunetApp"
    private val appScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    lateinit var bleManager: LunetBleManager
        private set
    lateinit var mediaBridge: MediaBridgeManager
        private set
    lateinit var telephonyBridge: TelephonyBridgeManager
        private set
    lateinit var phoneFinder: PhoneFinderManager
        private set
    lateinit var syncEngine: SyncEngine
        private set

    // Feature toggles for Bridge screen
    val syncMusicEnabled = MutableStateFlow(true)
    val syncNotifsEnabled = MutableStateFlow(true)
    val syncNavEnabled = MutableStateFlow(true)
    val syncCallsEnabled = MutableStateFlow(true)
    val syncTimeBattWeather = MutableStateFlow(true)
    val autoReconnectEnabled = MutableStateFlow(true)

    // Cluster telemetry state
    private val _telemetry = MutableStateFlow(ClusterTelemetry())
    val telemetry: StateFlow<ClusterTelemetry> = _telemetry.asStateFlow()

    // Notification listener active flag
    private val _isNotificationListenerActive = MutableStateFlow(false)
    val isNotificationListenerActive: StateFlow<Boolean> = _isNotificationListenerActive.asStateFlow()

    private var syncPeriodicJob: Job? = null

    override fun onCreate() {
        super.onCreate()
        instance = this

        bleManager = LunetBleManager(this)
        mediaBridge = MediaBridgeManager(this)
        phoneFinder = PhoneFinderManager(this)
        syncEngine = SyncEngine(this) { packet ->
            bleManager.enqueuePacket(packet)
        }

        telephonyBridge = TelephonyBridgeManager(this) { callPacket ->
            if (syncCallsEnabled.value) {
                bleManager.enqueuePacket(callPacket)
            }
        }

        observeIncomingBlePackets()
        observeMediaChanges()
        observeConnectionState()
    }

    fun setNotificationListenerActive(active: Boolean) {
        _isNotificationListenerActive.value = active
    }

    fun startBleForegroundService() {
        val serviceIntent = Intent(this, LunetBleService::class.java).apply {
            action = LunetBleService.ACTION_CONNECT
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(serviceIntent)
        } else {
            startService(serviceIntent)
        }
    }

    private fun observeConnectionState() {
        appScope.launch {
            bleManager.connectionState.collectLatest { state ->
                if (state == BleConnectionState.CONNECTED) {
                    delay(300)
                    if (syncTimeBattWeather.value) {
                        syncEngine.triggerFullSync()
                    }
                    if (syncMusicEnabled.value) {
                        bleManager.enqueuePacket(mediaBridge.formatMediaPacket())
                    }
                    startPeriodicSync()
                } else {
                    stopPeriodicSync()
                }
            }
        }
    }

    private fun startPeriodicSync() {
        syncPeriodicJob?.cancel()
        syncPeriodicJob = appScope.launch {
            while (isActive && bleManager.connectionState.value == BleConnectionState.CONNECTED) {
                delay(60000)
                if (syncTimeBattWeather.value) {
                    syncEngine.triggerFullSync()
                }
            }
        }
    }

    private fun stopPeriodicSync() {
        syncPeriodicJob?.cancel()
        syncPeriodicJob = null
    }

    private fun observeMediaChanges() {
        appScope.launch {
            mediaBridge.playbackInfo.collectLatest { info ->
                _telemetry.value = _telemetry.value.copy(
                    nowPlayingTitle = info.title,
                    nowPlayingArtist = info.artist,
                    isMediaPlaying = info.isPlaying,
                    mediaProgress = info.progressPercent,
                    mediaVolume = info.volumePercent
                )
                if (syncMusicEnabled.value && bleManager.connectionState.value == BleConnectionState.CONNECTED) {
                    bleManager.enqueuePacket(mediaBridge.formatMediaPacket())
                }
            }
        }
    }

    private fun observeIncomingBlePackets() {
        appScope.launch {
            bleManager.incomingPackets.collect { rawPkt ->
                handleIncomingClusterPacket(rawPkt)
            }
        }
    }

    private fun handleIncomingClusterPacket(pkt: String) {
        Log.i(tag, "Dispatching incoming cluster packet: $pkt")
        when {
            pkt.startsWith("CMD:MEDIA:") || pkt.startsWith("CMD:VOL:") || pkt.startsWith("CMD:EQ:") -> {
                mediaBridge.handleClusterCommand(pkt)
            }
            pkt == "CMD:CALL:ACCEPT" -> telephonyBridge.acceptCall()
            pkt == "CMD:CALL:DECLINE" -> telephonyBridge.declineCall()
            pkt == "CMD:PHONE:PING" -> phoneFinder.triggerAlarm("ESP32 Cluster Ping")
            pkt == "CMD:SYNC:REQ" -> {
                if (syncTimeBattWeather.value) {
                    syncEngine.triggerFullSync()
                }
                if (syncMusicEnabled.value) {
                    bleManager.enqueuePacket(mediaBridge.formatMediaPacket())
                }
            }
            pkt.startsWith("EVT:GEAR:") -> {
                val gChar = pkt.substringAfter("EVT:GEAR:").trim().firstOrNull() ?: 'P'
                val gear = when (gChar) {
                    'P' -> Gear.PARK
                    'R' -> Gear.REVERSE
                    'N' -> Gear.NEUTRAL
                    'D' -> Gear.DRIVE
                    else -> Gear.PARK
                }
                _telemetry.value = _telemetry.value.copy(gear = gear)
            }
            pkt.startsWith("EVT:MODE:") -> {
                val mStr = pkt.substringAfter("EVT:MODE:").trim()
                val mode = when (mStr) {
                    "ECO" -> DriveMode.ECO
                    "SPORT" -> DriveMode.SPORT
                    else -> DriveMode.COMFORT
                }
                _telemetry.value = _telemetry.value.copy(mode = mode)
            }
        }
    }

    fun onNotificationCaptured(appName: String, message: String) {
        if (!syncNotifsEnabled.value) return
        val clampedMsg = message.take(60)
        val notifPacket = "NOTIF:$appName|$clampedMsg"
        _telemetry.value = _telemetry.value.copy(
            latestAlertText = "$appName: $clampedMsg"
        )
        bleManager.enqueuePacket(notifPacket)
    }

    fun onNavigationUpdated(
        maneuver: String,
        distMeters: Int,
        street: String,
        etaMin: Int,
        remKm: Float,
        rawPacket: String
    ) {
        val dir = when (maneuver) {
            "LEFT" -> NavDirection.LEFT
            "RIGHT" -> NavDirection.RIGHT
            "UTURN" -> NavDirection.UTURN
            else -> NavDirection.STRAIGHT
        }
        _telemetry.value = _telemetry.value.copy(
            navDirection = dir,
            navDistanceM = distMeters,
            navStreet = street,
            navEtaMin = etaMin,
            navRemKm = remKm
        )
        if (syncNavEnabled.value) {
            bleManager.enqueuePacket(rawPacket)
        }
    }

    // Demo Remote Controls
    fun setSpeed(spd: Float) {
        _telemetry.value = _telemetry.value.copy(speedKmh = spd)
        bleManager.enqueuePacket(String.format(java.util.Locale.US, "SPEED:%.1f", spd))
    }

    fun setSpeedLimit(limit: Int) {
        _telemetry.value = _telemetry.value.copy(speedLimitKmh = limit)
        bleManager.enqueuePacket("LIMIT:$limit")
    }

    fun setGear(gear: Gear) {
        _telemetry.value = _telemetry.value.copy(gear = gear)
        bleManager.enqueuePacket("GEAR:${gear.code}")
    }

    fun setDriveMode(mode: DriveMode) {
        _telemetry.value = _telemetry.value.copy(mode = mode)
        bleManager.enqueuePacket("MODE:${mode.label}")
    }

    fun setSoc(soc: Float) {
        _telemetry.value = _telemetry.value.copy(evSocPercent = soc)
        bleManager.enqueuePacket(String.format(java.util.Locale.US, "SOC:%.1f", soc))
    }

    companion object {
        var instance: LunetApp? = null
            private set
    }
}
