package com.visteon.lunet.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.visteon.lunet.LunetApp
import com.visteon.lunet.ble.BleConnectionState
import com.visteon.lunet.ble.BleLogEntry
import com.visteon.lunet.ble.ClusterTelemetry
import com.visteon.lunet.ble.DriveMode
import com.visteon.lunet.ble.Gear
import com.visteon.lunet.ble.NavDirection
import com.visteon.lunet.bridge.CallState
import com.visteon.lunet.bridge.MediaPlaybackInfo
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

enum class ScreenTab(val title: String) {
    HOME("Home"),
    SYNC("Sync"),
    DEMO("Demo")
}

class LunetViewModel(application: Application) : AndroidViewModel(application) {

    private val app = application as LunetApp

    val connectionState: StateFlow<BleConnectionState> = app.bleManager.connectionState
    val rssi: StateFlow<Int> = app.bleManager.rssi
    val mtu: StateFlow<Int> = app.bleManager.negotiatedMtu
    val packetLogs: StateFlow<List<BleLogEntry>> = app.bleManager.packetLogs

    val telemetry: StateFlow<ClusterTelemetry> = app.telemetry
    val mediaInfo: StateFlow<MediaPlaybackInfo> = app.mediaBridge.playbackInfo
    val callState: StateFlow<CallState> = app.telephonyBridge.callState
    val weather: StateFlow<String> = app.syncEngine.weatherState
    val isAlarmActive: StateFlow<Boolean> = app.phoneFinder.isAlarmActive
    val isNotificationListenerActive: StateFlow<Boolean> = app.isNotificationListenerActive
    val recentNotifications: StateFlow<List<com.visteon.lunet.ble.NotifMirrorItem>> = app.recentNotifications

    // Sync toggles
    val syncMusic = app.syncMusicEnabled
    val syncNotifs = app.syncNotifsEnabled
    val syncNav = app.syncNavEnabled
    val syncCalls = app.syncCallsEnabled
    val syncTimeBattWeather = app.syncTimeBattWeather
    val autoReconnect = app.autoReconnectEnabled

    // UI state
    val showBleConsoleSheet = MutableStateFlow(false)

    private val _currentTab = MutableStateFlow(ScreenTab.HOME)
    val currentTab: StateFlow<ScreenTab> = _currentTab.asStateFlow()

    private val _isDarkMode = MutableStateFlow(true)
    val isDarkMode: StateFlow<Boolean> = _isDarkMode.asStateFlow()

    fun selectTab(tab: ScreenTab) {
        _currentTab.value = tab
    }

    fun toggleTheme() {
        _isDarkMode.value = !_isDarkMode.value
    }

    // BLE Actions
    fun connect() {
        app.startBleForegroundService()
        app.bleManager.startScan()
    }

    fun disconnect() {
        app.bleManager.disconnect()
    }

    fun sendRawCommand(raw: String) {
        app.bleManager.enqueuePacket(raw)
    }

    fun clearLogs() {
        app.bleManager.clearLogs()
    }

    // Ping / Finder
    fun triggerTestPing() {
        app.phoneFinder.triggerAlarm("Manual Test Ping")
    }

    fun stopAlarm() {
        app.phoneFinder.stopAlarm()
    }

    // Sync Actions
    fun triggerFullSync() {
        app.syncEngine.triggerFullSync()
    }

    fun fetchWeather() {
        app.syncEngine.fetchLiveWeather()
    }

    fun setManualWeather(override: String) {
        app.syncEngine.setManualWeatherOverride(override)
        app.bleManager.enqueuePacket("WEATHER:$override")
    }

    // Demo Remote Controls
    fun setSpeed(spd: Float) {
        app.setSpeed(spd)
    }

    fun setSpeedLimit(limit: Int) {
        app.setSpeedLimit(limit)
    }

    fun setGear(gear: Gear) {
        app.setGear(gear)
    }

    fun setDriveMode(mode: DriveMode) {
        app.setDriveMode(mode)
    }

    fun setSoc(soc: Float) {
        app.setSoc(soc)
    }

    // Media Controls
    fun play() = app.mediaBridge.play()
    fun pause() = app.mediaBridge.pause()
    fun next() = app.mediaBridge.next()
    fun prev() = app.mediaBridge.prev()
    fun seekTo(pct: Int) = app.mediaBridge.seekTo(pct)
    fun setVolume(pct: Int) = app.mediaBridge.setVolumePercent(pct)

    // Demo Scenarios for Visteon Presentations
    fun simulateIncomingCall(caller: String = "Aditya Sharma") {
        app.bleManager.enqueuePacket("CALL:INCOMING|$caller")
    }

    fun simulateActiveCall(caller: String = "Aditya Sharma") {
        app.bleManager.enqueuePacket("CALL:ACTIVE|$caller")
    }

    fun simulateEndCall() {
        app.bleManager.enqueuePacket("CALL:ENDED")
    }

    fun sendSampleNotification(
        appName: String = "WhatsApp",
        msg: String = "Team: Product Review at 4:30 PM with VP"
    ) {
        app.onNotificationCaptured(appName, msg)
    }

    fun pushSampleNavTurn(
        dir: NavDirection = NavDirection.RIGHT,
        distM: Int = 180,
        street: String = "Visteon Innovation Way",
        etaMin: Int = 12,
        remKm: Float = 7.3f
    ) {
        val navPacket = "NAV:${dir.name}|$distM|$street|$etaMin|$remKm"
        app.onNavigationUpdated(dir.name, distM, street, etaMin, remKm, navPacket)
    }
}
