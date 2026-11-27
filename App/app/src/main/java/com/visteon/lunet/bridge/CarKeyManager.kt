package com.visteon.lunet.bridge

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.media.AudioManager
import android.media.ToneGenerator
import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import java.util.UUID

object CarKeyBleConstants {
    // Advertised Device Names for generic iTAG / iSearching fobs
    val TARGET_NAMES = listOf("itag", "isearching", "smart tag", "c-tag")

    // GATT Services & Characteristics
    val SERVICE_VENDOR: UUID = UUID.fromString("0000ffe0-0000-1000-8000-00805f9b34fb")
    val CHAR_BUTTON_NOTIFY: UUID = UUID.fromString("0000ffe1-0000-1000-8000-00805f9b34fb")
    val CHAR_COMMAND_WRITE: UUID = UUID.fromString("0000ffe2-0000-1000-8000-00805f9b34fb")

    val SERVICE_IMMEDIATE_ALERT: UUID = UUID.fromString("00001802-0000-1000-8000-00805f9b34fb")
    val CHAR_ALERT_LEVEL: UUID = UUID.fromString("00002a06-0000-1000-8000-00805f9b34fb")

    val SERVICE_BATTERY: UUID = UUID.fromString("0000180f-0000-1000-8000-00805f9b34fb")
    val CHAR_BATTERY_LEVEL: UUID = UUID.fromString("00002a19-0000-1000-8000-00805f9b34fb")

    val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    // Button Timing Logic Thresholds (per specification)
    const val BOUNCE_FILTER_MS = 100L
    const val SINGLE_CLICK_WINDOW_MS = 400L
    const val DOUBLE_CLICK_WINDOW_MS = 500L
}

enum class CarKeyConnectionState {
    DISCONNECTED,
    SCANNING,
    CONNECTING,
    CONNECTED
}

enum class CarKeyEvent {
    SINGLE_CLICK,
    DOUBLE_CLICK
}

/**
 * Dedicated Bluetooth Low Energy Manager for the generic iTAG / iSearching key fob tracker.
 *
 * Functions as an automotive 'Car Key':
 * - Connects alongside the Visteon cluster over BLE.
 * - Subscribes to 0xFFE1 button notifications.
 * - Hardware debounce (< 100ms) + Single Click confirmation (400ms) -> Locks/Unlocks car.
 * - Double Click window (100ms - 500ms) -> Triggers buzzer alarm (hardware Immediate Alert + phone sound/vibration simulation).
 * - Reads CR2032 battery level via Battery Service 0x180F.
 */
class CarKeyManager(
    private val context: Context,
    private val onKeyEvent: (event: CarKeyEvent, isLocked: Boolean) -> Unit
) {
    private val tag = "CarKeyManager"
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    private val bluetoothManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
    private val bluetoothAdapter: BluetoothAdapter? get() = bluetoothManager?.adapter

    private var activeGatt: BluetoothGatt? = null
    private var alertLevelChar: BluetoothGattCharacteristic? = null
    private var cmdWriteChar: BluetoothGattCharacteristic? = null
    private var isScanning = false

    private val prefs = context.getSharedPreferences("carkey_ble_prefs", Context.MODE_PRIVATE)

    // StateFlows for UI
    private val _connectionState = MutableStateFlow(CarKeyConnectionState.DISCONNECTED)
    val connectionState: StateFlow<CarKeyConnectionState> = _connectionState.asStateFlow()

    private val _isCarLocked = MutableStateFlow(true)
    val isCarLocked: StateFlow<Boolean> = _isCarLocked.asStateFlow()

    private val _batteryLevel = MutableStateFlow<Int?>(null)
    val batteryLevel: StateFlow<Int?> = _batteryLevel.asStateFlow()

    private val _isBuzzerActive = MutableStateFlow(false)
    val isBuzzerActive: StateFlow<Boolean> = _isBuzzerActive.asStateFlow()

    private val _lastActionText = MutableStateFlow("Car Key Standby")
    val lastActionText: StateFlow<String> = _lastActionText.asStateFlow()

    private val _deviceName = MutableStateFlow("Car Key")
    val deviceName: StateFlow<String> = _deviceName.asStateFlow()

    // Button Timing State
    private var lastRawPressTime = 0L
    private var firstPressTime = 0L
    private var pendingSingleClickJob: Job? = null
    private var buzzerJob: Job? = null

    // Phone simulation buzzer audio & haptics
    private var toneGenerator: ToneGenerator? = null
    private val vibrator: Vibrator? get() {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val vm = context.getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as? VibratorManager
            vm?.defaultVibrator
        } else {
            @Suppress("DEPRECATION")
            context.getSystemService(Context.VIBRATOR_SERVICE) as? Vibrator
        }
    }

    init {
        // Automatically start scanning or connect if previously paired
        autoConnectIfSaved()
    }

    fun autoConnectIfSaved() {
        val lastMac = prefs.getString("last_carkey_mac", null)
        if (!lastMac.isNullOrBlank() && bluetoothAdapter?.isEnabled == true) {
            try {
                val device = bluetoothAdapter?.getRemoteDevice(lastMac)
                if (device != null) {
                    Log.i(tag, "Attempting direct connect to saved Car Key ($lastMac)")
                    connectToDevice(device)
                    return
                }
            } catch (e: Exception) {
                Log.w(tag, "Could not direct-connect to saved MAC: ${e.message}")
            }
        }
        // Fallback: start scan
        startScan()
    }

    @SuppressLint("MissingPermission")
    fun startScan() {
        val scanner = bluetoothAdapter?.bluetoothLeScanner
        if (scanner == null || isScanning || _connectionState.value == CarKeyConnectionState.CONNECTED) return

        _connectionState.value = CarKeyConnectionState.SCANNING
        isScanning = true

        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .setReportDelay(0)
            .build()

        try {
            scanner.startScan(null, settings, scanCallback)
            Log.i(tag, "Car Key BLE scan initiated")

            // Auto stop scan after 20 seconds
            scope.launch {
                delay(20000)
                if (isScanning && _connectionState.value == CarKeyConnectionState.SCANNING) {
                    stopScan()
                    _connectionState.value = CarKeyConnectionState.DISCONNECTED
                }
            }
        } catch (e: SecurityException) {
            Log.e(tag, "Missing BLE permissions: ${e.message}")
            _connectionState.value = CarKeyConnectionState.DISCONNECTED
            isScanning = false
        }
    }

    @SuppressLint("MissingPermission")
    fun stopScan() {
        if (!isScanning) return
        try {
            bluetoothAdapter?.bluetoothLeScanner?.stopScan(scanCallback)
        } catch (e: Exception) {
            Log.w(tag, "Error stopping scan: ${e.message}")
        } finally {
            isScanning = false
        }
    }

    @SuppressLint("MissingPermission")
    fun connectToDevice(device: BluetoothDevice) {
        stopScan()
        _connectionState.value = CarKeyConnectionState.CONNECTING
        Log.i(tag, "Connecting to Car Key: ${device.name ?: device.address} (${device.address})...")

        val name = device.name ?: "Car Key"
        _deviceName.value = name

        // Save last known MAC
        prefs.edit().putString("last_carkey_mac", device.address).apply()

        // Clean up prior GATT
        activeGatt?.let { gatt ->
            try {
                gatt.disconnect()
                gatt.close()
            } catch (e: Exception) {
                Log.w(tag, "Error closing prior Car Key GATT: ${e.message}")
            }
        }
        activeGatt = null

        scope.launch(Dispatchers.Main) {
            delay(150) // Socket stabilization delay
            try {
                activeGatt = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                    device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
                } else {
                    device.connectGatt(context, false, gattCallback)
                }
            } catch (e: Exception) {
                Log.e(tag, "connectGatt failed for Car Key: ${e.message}")
                _connectionState.value = CarKeyConnectionState.DISCONNECTED
            }
        }
    }

    @SuppressLint("MissingPermission")
    fun disconnect() {
        stopScan()
        activeGatt?.let { gatt ->
            try {
                gatt.disconnect()
                gatt.close()
            } catch (e: Exception) {
                Log.w(tag, "Error closing Car Key gatt: ${e.message}")
            }
        }
        activeGatt = null
        alertLevelChar = null
        cmdWriteChar = null
        _connectionState.value = CarKeyConnectionState.DISCONNECTED
        _batteryLevel.value = null
    }

    private val scanCallback = object : ScanCallback() {
        @SuppressLint("MissingPermission")
        override fun onScanResult(callbackType: Int, result: ScanResult?) {
            result?.device?.let { device ->
                val name = (device.name ?: result.scanRecord?.deviceName ?: "").lowercase()
                val hasVendorService = result.scanRecord?.serviceUuids?.any {
                    it.uuid == CarKeyBleConstants.SERVICE_VENDOR || it.uuid == CarKeyBleConstants.SERVICE_IMMEDIATE_ALERT
                } == true

                val savedMac = prefs.getString("last_carkey_mac", null)
                val matchesMac = savedMac != null && device.address.equals(savedMac, ignoreCase = true)

                val matchesName = CarKeyBleConstants.TARGET_NAMES.any { name.contains(it) }

                if (matchesName || hasVendorService || matchesMac) {
                    Log.i(tag, "Discovered target Car Key: '${device.name}' (${device.address})")
                    connectToDevice(device)
                }
            }
        }

        override fun onScanFailed(errorCode: Int) {
            Log.e(tag, "Car Key scan failed: $errorCode")
            isScanning = false
            _connectionState.value = CarKeyConnectionState.DISCONNECTED
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            Log.d(tag, "CarKey onConnectionStateChange: status=$status, newState=$newState")
            if (status != BluetoothGatt.GATT_SUCCESS && newState != BluetoothProfile.STATE_CONNECTED) {
                Log.w(tag, "CarKey GATT error $status, closing handle")
                try {
                    gatt.close()
                } catch (e: Exception) {
                    // ignore
                }
                if (activeGatt == gatt) activeGatt = null
                _connectionState.value = CarKeyConnectionState.DISCONNECTED
                return
            }

            if (newState == BluetoothProfile.STATE_CONNECTED) {
                _connectionState.value = CarKeyConnectionState.CONNECTED
                _lastActionText.value = "Car Key Connected"
                Log.i(tag, "Connected to Car Key fob. Waiting 400ms stabilization delay before discoverServices...")
                scope.launch {
                    delay(400) // 300ms - 500ms stabilization delay per spec
                    gatt.discoverServices()
                }
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                Log.w(tag, "Car Key disconnected")
                try {
                    gatt.close()
                } catch (e: Exception) {
                    Log.w(tag, "Error closing gatt: ${e.message}")
                }
                if (activeGatt == gatt) activeGatt = null
                alertLevelChar = null
                cmdWriteChar = null
                _connectionState.value = CarKeyConnectionState.DISCONNECTED
                _lastActionText.value = "Car Key Disconnected"
            }
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.e(tag, "CarKey service discovery failed: $status")
                return
            }

            Log.i(tag, "Car Key services discovered successfully")

            // 1. Setup 0xFFE0 / 0xFFE1 Button Notifications
            val vendorService = gatt.getService(CarKeyBleConstants.SERVICE_VENDOR)
            if (vendorService != null) {
                val buttonChar = vendorService.getCharacteristic(CarKeyBleConstants.CHAR_BUTTON_NOTIFY)
                if (buttonChar != null) {
                    gatt.setCharacteristicNotification(buttonChar, true)
                    val descriptor = buttonChar.getDescriptor(CarKeyBleConstants.CCCD_UUID)
                    if (descriptor != null) {
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                            gatt.writeDescriptor(descriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                        } else {
                            @Suppress("DEPRECATION")
                            descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                            @Suppress("DEPRECATION")
                            gatt.writeDescriptor(descriptor)
                        }
                        Log.i(tag, "Enabled CCCD notifications on Car Key button 0xFFE1")
                    }
                }
                cmdWriteChar = vendorService.getCharacteristic(CarKeyBleConstants.CHAR_COMMAND_WRITE)
            }

            // 2. Setup Immediate Alert 0x1802 / 0x2A06 (Buzzer)
            val alertService = gatt.getService(CarKeyBleConstants.SERVICE_IMMEDIATE_ALERT)
            if (alertService != null) {
                alertLevelChar = alertService.getCharacteristic(CarKeyBleConstants.CHAR_ALERT_LEVEL)
                Log.i(tag, "Found Car Key Immediate Alert characteristic 0x2A06")
            }

            // 3. Read Battery Level 0x180F / 0x2A19
            scope.launch {
                delay(300)
                val batteryService = gatt.getService(CarKeyBleConstants.SERVICE_BATTERY)
                val batteryChar = batteryService?.getCharacteristic(CarKeyBleConstants.CHAR_BATTERY_LEVEL)
                if (batteryChar != null) {
                    gatt.readCharacteristic(batteryChar)
                }
            }
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            if (characteristic.uuid == CarKeyBleConstants.CHAR_BUTTON_NOTIFY) {
                @Suppress("DEPRECATION")
                val value = characteristic.value
                handleButtonNotification(value)
            }
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray
        ) {
            if (characteristic.uuid == CarKeyBleConstants.CHAR_BUTTON_NOTIFY) {
                handleButtonNotification(value)
            }
        }

        override fun onCharacteristicRead(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS && characteristic.uuid == CarKeyBleConstants.CHAR_BATTERY_LEVEL) {
                @Suppress("DEPRECATION")
                val data = characteristic.value
                if (data != null && data.isNotEmpty()) {
                    val pct = data[0].toInt() and 0xFF
                    _batteryLevel.value = pct.coerceIn(0, 100)
                    Log.i(tag, "Car Key Battery Level: $pct%")
                }
            }
        }
    }

    /**
     * Physical Button Event Handler
     *
     * Flow per specification:
     * 1. Check contact bounce: drop packet if < 100 ms from last packet.
     * 2. If a pending click timer is active within [100ms..500ms]:
     *    -> Cancel single click timer, dispatch DOUBLE CLICK (Buzzer simulation).
     * 3. Else start a 400ms timer:
     *    -> If no 2nd press occurs, dispatch SINGLE CLICK (Car Lock/Unlock toggle).
     */
    private fun handleButtonNotification(payload: ByteArray?) {
        val now = System.currentTimeMillis()

        // 1. Contact Bounce Filter (< 100 ms)
        if (now - lastRawPressTime < CarKeyBleConstants.BOUNCE_FILTER_MS) {
            Log.d(tag, "Contact bounce filter dropped packet (${now - lastRawPressTime} ms)")
            return
        }
        lastRawPressTime = now

        val activeJob = pendingSingleClickJob
        if (activeJob != null && activeJob.isActive) {
            val interval = now - firstPressTime
            if (interval <= CarKeyBleConstants.DOUBLE_CLICK_WINDOW_MS) {
                // Double Click Detected!
                activeJob.cancel()
                pendingSingleClickJob = null
                Log.i(tag, "Car Key DOUBLE CLICK detected (interval: $interval ms)")
                scope.launch(Dispatchers.Main) {
                    triggerBuzzer()
                }
                return
            }
        }

        // First click: start single-click confirmation timer (400 ms)
        firstPressTime = now
        pendingSingleClickJob?.cancel()
        pendingSingleClickJob = scope.launch(Dispatchers.Main) {
            delay(CarKeyBleConstants.SINGLE_CLICK_WINDOW_MS)
            Log.i(tag, "Car Key SINGLE CLICK confirmed (no 2nd press in 400 ms)")
            toggleCarLock()
            pendingSingleClickJob = null
        }
    }

    /**
     * Toggles car lock state.
     * Single click: lock car, or if already locked, unlock it.
     */
    fun toggleCarLock(): Boolean {
        val next = !_isCarLocked.value
        _isCarLocked.value = next
        _lastActionText.value = if (next) "Car Locked" else "Car Unlocked"
        Log.i(tag, "Car Lock toggled: isLocked = $next")

        // Phone haptic confirmation
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                vibrator?.vibrate(VibrationEffect.createOneShot(70, VibrationEffect.DEFAULT_AMPLITUDE))
            } else {
                @Suppress("DEPRECATION")
                vibrator?.vibrate(70)
            }
        } catch (e: Exception) {
            Log.w(tag, "Vibration failed: ${e.message}")
        }

        onKeyEvent(CarKeyEvent.SINGLE_CLICK, next)
        return next
    }

    fun setCarLockedFromCluster(locked: Boolean) {
        _isCarLocked.value = locked
        _lastActionText.value = if (locked) "Car Locked" else "Car Unlocked"
    }

    /**
     * Double Click Action: Ring buzzer simulation.
     * - Writes High Alert (0x02) to Immediate Alert 0x2A06 on tracker hardware.
     * - Runs phone buzzer sound (ToneGenerator) + pulsing vibration pattern for 2.5s.
     * - Auto-silences after 2.5 seconds.
     */
    fun triggerBuzzer() {
        if (_isBuzzerActive.value) {
            stopBuzzer()
            return
        }

        Log.i(tag, "Triggering Car Key Buzzer Simulation")
        _isBuzzerActive.value = true
        _lastActionText.value = "Buzzer Ringing..."

        // 1. Hardware Write to iTAG (Immediate Alert 0x2A06)
        writeBuzzerPayload(0x02.toByte())

        // 2. Phone Audio Tone Simulation
        try {
            toneGenerator?.release()
            toneGenerator = ToneGenerator(AudioManager.STREAM_ALARM, 85).apply {
                startTone(ToneGenerator.TONE_CDMA_ALERT_CALL_GUARD, 2400)
            }
        } catch (e: Exception) {
            Log.w(tag, "ToneGenerator error: ${e.message}")
        }

        // 3. Phone Pulsing Vibration Waveform
        try {
            val timings = longArrayOf(0, 180, 80, 180, 80, 300, 80, 180)
            val amplitudes = intArrayOf(0, 255, 0, 255, 0, 255, 0, 255)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                vibrator?.vibrate(VibrationEffect.createWaveform(timings, amplitudes, -1))
            } else {
                @Suppress("DEPRECATION")
                vibrator?.vibrate(timings, -1)
            }
        } catch (e: Exception) {
            Log.w(tag, "Vibration failed: ${e.message}")
        }

        onKeyEvent(CarKeyEvent.DOUBLE_CLICK, _isCarLocked.value)

        // 4. Auto-stop buzzer after 2.5 seconds
        buzzerJob?.cancel()
        buzzerJob = scope.launch(Dispatchers.Main) {
            delay(2500)
            stopBuzzer()
        }
    }

    fun stopBuzzer() {
        if (!_isBuzzerActive.value) return
        Log.i(tag, "Stopping Car Key Buzzer")
        _isBuzzerActive.value = false
        _lastActionText.value = "Buzzer Silenced"

        buzzerJob?.cancel()
        buzzerJob = null

        // Stop hardware buzzer (write 0x00)
        writeBuzzerPayload(0x00.toByte())

        try {
            toneGenerator?.stopTone()
            toneGenerator?.release()
            toneGenerator = null
        } catch (e: Exception) {
            // ignore
        }

        try {
            vibrator?.cancel()
        } catch (e: Exception) {
            // ignore
        }
    }

    @SuppressLint("MissingPermission")
    private fun writeBuzzerPayload(level: Byte) {
        val gatt = activeGatt ?: return
        val char = alertLevelChar ?: return
        try {
            val payload = byteArrayOf(level)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                gatt.writeCharacteristic(char, payload, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE)
            } else {
                @Suppress("DEPRECATION")
                char.value = payload
                @Suppress("DEPRECATION")
                char.writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
                @Suppress("DEPRECATION")
                gatt.writeCharacteristic(char)
            }

            // Also write to secondary custom buzzer char 0xFFE2 if available
            cmdWriteChar?.let { cChar ->
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    gatt.writeCharacteristic(cChar, payload, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE)
                } else {
                    @Suppress("DEPRECATION")
                    cChar.value = payload
                    @Suppress("DEPRECATION")
                    gatt.writeCharacteristic(cChar)
                }
            }
        } catch (e: Exception) {
            Log.w(tag, "Failed to write buzzer payload to Car Key: ${e.message}")
        }
    }
}
