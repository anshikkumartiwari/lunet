package com.visteon.lunet.bridge

import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.BufferedReader
import java.io.InputStreamReader
import java.net.HttpURLConnection
import java.net.URL
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Automates synchronization of Phone Time, Date, Battery Level, and Live Weather
 * to the Visteon Lunet OS 3.0 cluster.
 */
class SyncEngine(
    private val context: Context,
    private val onPacketReady: (String) -> Unit
) {
    private val tag = "SyncEngine"
    private val scope = CoroutineScope(Dispatchers.IO)

    private val _weatherState = MutableStateFlow("26°C Clear")
    val weatherState: StateFlow<String> = _weatherState.asStateFlow()

    private var manualWeatherOverride: String? = null

    fun getFormattedTime(): String {
        return SimpleDateFormat("hh:mm a", Locale.getDefault()).format(Date())
    }

    fun getFormattedDate(): String {
        return SimpleDateFormat("EEE, dd MMM", Locale.getDefault()).format(Date())
    }

    fun getPhoneBatteryLevel(): Int {
        val ifilter = IntentFilter(Intent.ACTION_BATTERY_CHANGED)
        val batteryStatus: Intent? = context.registerReceiver(null, ifilter)
        val level: Int = batteryStatus?.getIntExtra(BatteryManager.EXTRA_LEVEL, -1) ?: -1
        val scale: Int = batteryStatus?.getIntExtra(BatteryManager.EXTRA_SCALE, -1) ?: -1
        return if (level >= 0 && scale > 0) {
            ((level.toFloat() / scale.toFloat()) * 100).toInt().coerceIn(0, 100)
        } else {
            100
        }
    }

    fun setManualWeatherOverride(override: String?) {
        manualWeatherOverride = override
        if (!override.isNullOrBlank()) {
            _weatherState.value = override
        }
    }

    fun triggerFullSync() {
        val timePkt = "TIME:${getFormattedTime()}"
        val datePkt = "DATE:${getFormattedDate()}"
        val battPkt = "PBATT:${getPhoneBatteryLevel()}"
        val weatherPkt = "WEATHER:${manualWeatherOverride ?: _weatherState.value}"

        Log.i(tag, "Dispatching full cluster sync: $timePkt | $datePkt | $battPkt | $weatherPkt")
        onPacketReady(timePkt)
        onPacketReady(datePkt)
        onPacketReady(battPkt)
        onPacketReady(weatherPkt)
    }

    fun fetchLiveWeather(lat: Double = 12.9716, lon: Double = 77.5946) {
        if (!manualWeatherOverride.isNullOrBlank()) return

        scope.launch {
            try {
                val endpoint = "https://api.open-meteo.com/v1/forecast?latitude=$lat&longitude=$lon&current=temperature_2m,weather_code"
                val url = URL(endpoint)
                val conn = withContext(Dispatchers.IO) {
                    (url.openConnection() as HttpURLConnection).apply {
                        requestMethod = "GET"
                        connectTimeout = 5000
                        readTimeout = 5000
                    }
                }

                if (conn.responseCode == HttpURLConnection.HTTP_OK) {
                    val reader = BufferedReader(InputStreamReader(conn.inputStream))
                    val response = reader.readText()
                    reader.close()

                    val json = JSONObject(response)
                    val current = json.getJSONObject("current")
                    val temp = current.getDouble("temperature_2m").toInt()
                    val code = current.getInt("weather_code")
                    val condition = mapWmoCode(code)

                    val weatherStr = "${temp}°C $condition"
                    _weatherState.value = weatherStr
                    onPacketReady("WEATHER:$weatherStr")
                    Log.i(tag, "Fetched live weather: $weatherStr")
                }
            } catch (e: Exception) {
                Log.w(tag, "Weather fetch fallback to offline demo: ${e.message}")
            }
        }
    }

    private fun mapWmoCode(code: Int): String {
        return when (code) {
            0 -> "Clear"
            1, 2, 3 -> "Partly Cloudy"
            45, 48 -> "Fog"
            51, 53, 55, 61, 63, 65 -> "Rain"
            71, 73, 75 -> "Snow"
            95, 96, 99 -> "Thunderstorm"
            else -> "Clear"
        }
    }
}
