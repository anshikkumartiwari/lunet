package com.visteon.lunet.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Build
import android.os.ParcelUuid
import android.util.Log
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull

/**
 * Rock-solid Native Android BluetoothGatt manager for Visteon Lunet OS 3.0.
 * Features:
 * - Serialized Coroutine Channel write queue awaiting onCharacteristicWrite
 * - Clamping to 150 bytes & UTF-8 sanitization
 * - MTU 247 negotiation
 * - Automatic descriptor CCCD enable
 * - Continuous live RSSI polling
 */
class LunetBleManager(private val context: Context) {

    private val tag = "LunetBleManager"

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val writeChannel = Channel<String>(Channel.UNLIMITED)
    private var writeJob: Job? = null
    private var rssiJob: Job? = null
    private var pendingWriteAck: CompletableDeferred<Boolean>? = null

    private val bluetoothManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
    private val bluetoothAdapter: BluetoothAdapter? get() = bluetoothManager?.adapter

    private var activeGatt: BluetoothGatt? = null
    private var lunetCharacteristic: BluetoothGattCharacteristic? = null

    // StateFlows for UI
    private val _connectionState = MutableStateFlow(BleConnectionState.DISCONNECTED)
    val connectionState: StateFlow<BleConnectionState> = _connectionState.asStateFlow()

    private val _rssi = MutableStateFlow(-60)
    val rssi: StateFlow<Int> = _rssi.asStateFlow()

    private val _negotiatedMtu = MutableStateFlow(23)
    val negotiatedMtu: StateFlow<Int> = _negotiatedMtu.asStateFlow()

    private val _incomingPackets = MutableSharedFlow<String>(extraBufferCapacity = 64)
    val incomingPackets: SharedFlow<String> = _incomingPackets.asSharedFlow()

    private val _packetLogs = MutableStateFlow<List<BleLogEntry>>(emptyList())
    val packetLogs: StateFlow<List<BleLogEntry>> = _packetLogs.asStateFlow()

    private var isScanning = false

    init {
        startWriteWorker()
    }

    private fun logPacket(direction: PacketDirection, payload: String) {
        val entry = BleLogEntry(direction = direction, payload = payload)
        val current = _packetLogs.value.toMutableList()
        current.add(0, entry)
        if (current.size > 200) current.removeAt(current.lastIndex)
        _packetLogs.value = current
    }

    fun clearLogs() {
        _packetLogs.value = emptyList()
    }

    private fun startWriteWorker() {
        writeJob?.cancel()
        writeJob = scope.launch {
            for (message in writeChannel) {
                sendSerializedPacket(message)
            }
        }
    }

    companion object {
        fun sanitizeField(value: String): String {
            return value.replace("|", "/").replace("\n", " ").replace("\r", " ").trim()
        }
    }

    fun enqueuePacket(rawPayload: String) {
        if (rawPayload.isBlank()) return
        writeChannel.trySend(rawPayload)
    }

    @SuppressLint("MissingPermission")
    private suspend fun sendSerializedPacket(rawPayload: String) {
        val gatt = activeGatt
        val char = lunetCharacteristic
        if (gatt == null || char == null || _connectionState.value != BleConnectionState.CONNECTED) {
            Log.w(tag, "Cannot send packet '$rawPayload': Not connected to Visteon cluster")
            return
        }

        // Clamp to 150 bytes and sanitize UTF-8
        val sanitized = rawPayload.take(BleConstants.MAX_PACKET_LENGTH).trim()
        val bytes = sanitized.toByteArray(Charsets.UTF_8)

        logPacket(PacketDirection.TX, sanitized)

        val ack = CompletableDeferred<Boolean>()
        pendingWriteAck = ack

        try {
            val initiated = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                gatt.writeCharacteristic(
                    char,
                    bytes,
                    BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                ) == BluetoothStatusCodes.SUCCESS
            } else {
                @Suppress("DEPRECATION")
                char.value = bytes
                @Suppress("DEPRECATION")
                char.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                @Suppress("DEPRECATION")
                gatt.writeCharacteristic(char)
            }

            if (!initiated) {
                Log.e(tag, "Failed to initiate write for: $sanitized")
                return
            }

            // Await callback with 500ms safety timeout
            withTimeoutOrNull(500) {
                ack.await()
            }
            // Small inter-packet cooldown to prevent ESP32 BLE buffer congestion
            delay(20)
        } catch (e: Exception) {
            Log.e(tag, "Exception during write: ${e.message}")
        } finally {
            pendingWriteAck = null
        }
    }

    @SuppressLint("MissingPermission")
    fun startScan() {
        val scanner = bluetoothAdapter?.bluetoothLeScanner
        if (scanner == null || isScanning) return

        _connectionState.value = BleConnectionState.SCANNING
        isScanning = true

        val filters = listOf(
            ScanFilter.Builder().setDeviceName(BleConstants.DEVICE_NAME).build(),
            ScanFilter.Builder().setServiceUuid(ParcelUuid(BleConstants.SERVICE_UUID)).build()
        )

        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()

        try {
            scanner.startScan(filters, settings, scanCallback)
            Log.i(tag, "BLE Scan initiated for '${BleConstants.DEVICE_NAME}'")

            // Auto stop scan after 15 seconds if not found
            scope.launch {
                delay(15000)
                if (isScanning && _connectionState.value == BleConnectionState.SCANNING) {
                    stopScan()
                    _connectionState.value = BleConnectionState.DISCONNECTED
                }
            }
        } catch (e: SecurityException) {
            Log.e(tag, "Missing BLE permissions: ${e.message}")
            _connectionState.value = BleConnectionState.DISCONNECTED
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
        _connectionState.value = BleConnectionState.CONNECTING
        Log.i(tag, "Connecting to ${device.name ?: device.address}...")

        activeGatt?.close()
        activeGatt = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
        } else {
            device.connectGatt(context, false, gattCallback)
        }
    }

    @SuppressLint("MissingPermission")
    fun disconnect() {
        stopRssiPolling()
        activeGatt?.let { gatt ->
            try {
                gatt.disconnect()
                gatt.close()
            } catch (e: Exception) {
                Log.w(tag, "Error closing gatt: ${e.message}")
            }
        }
        activeGatt = null
        lunetCharacteristic = null
        _connectionState.value = BleConnectionState.DISCONNECTED
        _negotiatedMtu.value = 23
    }

    private val scanCallback = object : ScanCallback() {
        @SuppressLint("MissingPermission")
        override fun onScanResult(callbackType: Int, result: ScanResult?) {
            result?.device?.let { device ->
                val name = device.name ?: result.scanRecord?.deviceName
                if (name == BleConstants.DEVICE_NAME ||
                    result.scanRecord?.serviceUuids?.any { it.uuid == BleConstants.SERVICE_UUID } == true
                ) {
                    Log.i(tag, "Discovered target cluster: $name (${device.address})")
                    connectToDevice(device)
                }
            }
        }

        override fun onScanFailed(errorCode: Int) {
            Log.e(tag, "BLE Scan failed with error code $errorCode")
            isScanning = false
            _connectionState.value = BleConnectionState.DISCONNECTED
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            Log.d(tag, "onConnectionStateChange: status=$status, newState=$newState")
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                Log.i(tag, "Connected to GATT server. Requesting MTU ${BleConstants.TARGET_MTU}...")
                gatt.requestMtu(BleConstants.TARGET_MTU)
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                Log.w(tag, "Disconnected from cluster GATT server.")
                stopRssiPolling()
                activeGatt = null
                lunetCharacteristic = null
                _connectionState.value = BleConnectionState.DISCONNECTED
                _negotiatedMtu.value = 23
            }
        }

        @SuppressLint("MissingPermission")
        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            Log.i(tag, "MTU changed to $mtu (status=$status)")
            _negotiatedMtu.value = mtu
            // Discover services now that MTU is confirmed
            gatt.discoverServices()
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.e(tag, "Service discovery failed with status $status")
                return
            }

            val service = gatt.getService(BleConstants.SERVICE_UUID)
            if (service == null) {
                Log.e(tag, "Service ${BleConstants.SERVICE_UUID} not found on cluster!")
                return
            }

            val characteristic = service.getCharacteristic(BleConstants.CHAR_UUID)
            if (characteristic == null) {
                Log.e(tag, "Characteristic ${BleConstants.CHAR_UUID} not found!")
                return
            }

            lunetCharacteristic = characteristic
            _connectionState.value = BleConnectionState.CONNECTED
            Log.i(tag, "Visteon Lunet Service & Characteristic identified. Subscribing to notifications...")

            // Enable local notifications
            gatt.setCharacteristicNotification(characteristic, true)

            // Write CCCD descriptor (0x2902) to notify ESP32
            val descriptor = characteristic.getDescriptor(BleConstants.CCCD_UUID)
            if (descriptor != null) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    gatt.writeDescriptor(descriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                } else {
                    @Suppress("DEPRECATION")
                    descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                    @Suppress("DEPRECATION")
                    gatt.writeDescriptor(descriptor)
                }
            }

            startRssiPolling()
        }

        override fun onCharacteristicWrite(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int
        ) {
            pendingWriteAck?.complete(status == BluetoothGatt.GATT_SUCCESS)
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            @Suppress("DEPRECATION")
            val data = characteristic.value ?: return
            handleIncomingBytes(data)
        }

        // Android 13+ overload
        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray
        ) {
            handleIncomingBytes(value)
        }

        @SuppressLint("MissingPermission")
        override fun onReadRemoteRssi(gatt: BluetoothGatt, rssi: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                _rssi.value = rssi
            }
        }
    }

    private fun handleIncomingBytes(bytes: ByteArray) {
        val text = String(bytes, Charsets.UTF_8).trim()
        if (text.isNotBlank()) {
            logPacket(PacketDirection.RX, text)
            _incomingPackets.tryEmit(text)
        }
    }

    @SuppressLint("MissingPermission")
    private fun startRssiPolling() {
        stopRssiPolling()
        rssiJob = scope.launch {
            while (isActive && _connectionState.value == BleConnectionState.CONNECTED) {
                delay(3000)
                try {
                    activeGatt?.readRemoteRssi()
                } catch (e: Exception) {
                    Log.w(tag, "Error reading RSSI: ${e.message}")
                }
            }
        }
    }

    private fun stopRssiPolling() {
        rssiJob?.cancel()
        rssiJob = null
    }
}
