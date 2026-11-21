package com.visteon.lunet.ble

import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.UUID

object BleConstants {
    const val DEVICE_NAME = "Visteon-Lunet"
    val SERVICE_UUID: UUID = UUID.fromString("7b5c0001-7b7c-4f2a-9a6d-2c0c0e4a1000")
    val CHAR_UUID: UUID = UUID.fromString("7b5c0002-7b7c-4f2a-9a6d-2c0c0e4a1000")
    val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    const val TARGET_MTU = 247
    const val MAX_PACKET_LENGTH = 150
}

enum class BleConnectionState {
    DISCONNECTED,
    SCANNING,
    CONNECTING,
    CONNECTED
}

enum class PacketDirection {
    RX,
    TX
}

data class BleLogEntry(
    val id: Long = System.nanoTime(),
    val timestamp: Long = System.currentTimeMillis(),
    val direction: PacketDirection,
    val payload: String
) {
    val formattedTime: String
        get() = SimpleDateFormat("HH:mm:ss.SSS", Locale.getDefault()).format(Date(timestamp))
}

enum class DriveMode(val label: String) {
    ECO("ECO"),
    COMFORT("COMFORT"),
    SPORT("SPORT")
}

enum class Gear(val code: Char) {
    PARK('P'),
    REVERSE('R'),
    NEUTRAL('N'),
    DRIVE('D')
}

enum class NavDirection {
    STRAIGHT,
    LEFT,
    RIGHT,
    UTURN
}

data class ClusterTelemetry(
    val speedKmh: Float = 0.0f,
    val speedLimitKmh: Int = 80,
    val gear: Gear = Gear.PARK,
    val mode: DriveMode = DriveMode.COMFORT,
    val evSocPercent: Float = 84.0f,
    val phoneBatteryPercent: Int = 100,
    val activeCallName: String = "",
    val isCallActive: Boolean = false,
    val isCallAnswered: Boolean = false,
    val nowPlayingTitle: String = "Lunet Symphony",
    val nowPlayingArtist: String = "Visteon Audio",
    val isMediaPlaying: Boolean = false,
    val mediaProgress: Int = 35,
    val mediaVolume: Int = 75,
    val navDirection: NavDirection = NavDirection.STRAIGHT,
    val navDistanceM: Int = 250,
    val navStreet: String = "Outer Ring Expressway",
    val navEtaMin: Int = 16,
    val navRemKm: Float = 11.4f,
    val latestAlertText: String = "",
    val latestAlertColor: Long = 0xFFF37B20
)

data class NotifMirrorItem(
    val id: String = java.util.UUID.randomUUID().toString(),
    val appName: String,
    val message: String,
    val timestamp: Long = System.currentTimeMillis()
) {
    val timeFormatted: String
        get() = SimpleDateFormat("h:mm a", Locale.getDefault()).format(Date(timestamp))
}
