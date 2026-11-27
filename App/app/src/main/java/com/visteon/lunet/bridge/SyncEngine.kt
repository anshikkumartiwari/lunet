package com.visteon.lunet.bridge

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationManager
import android.os.BatteryManager
import android.util.Log
import androidx.core.content.ContextCompat
import com.google.android.gms.location.FusedLocationProviderClient
import com.google.android.gms.location.LocationServices
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import org.json.JSONObject
import java.io.BufferedReader
import java.io.InputStreamReader
import java.net.HttpURLConnection
import java.net.URL
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.coroutines.resume

/**
 * Automates synchronization of Phone Time, Date, Battery Level, and Live Weather
 * to the Visteon Lunet OS 3.0 cluster.
 *
 * Weather Sync uses Open-Meteo free keyless API with GPS coordinates
 * via FusedLocationProviderClient (ACCESS_COARSE_LOCATION).
 */
class SyncEngine(
    private val context: Context,
    private val onPacketReady: (String) -> Unit
) {
    private val tag = "SyncEngine"
    private val scope = CoroutineScope(Dispatchers.IO)
    private val fusedLocationClient: FusedLocationProviderClient by lazy {
        LocationServices.getFusedLocationProviderClient(context)
    }

    private val _weatherState = MutableStateFlow("26C Clear")
    val weatherState: StateFlow<String> = _weatherState.asStateFlow()

    private var manualWeatherOverride: String? = null

    // Cached coordinates to fall back on if location is temporarily unavailable
    private var lastLat: Double = 12.9716
    private var lastLon: Double = 77.5946

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

    /**
     * Automatic Weather Sync via Open-Meteo:
     * 1. Fetches coordinates using FusedLocationProviderClient (ACCESS_COARSE_LOCATION).
     * 2. Calls https://api.open-meteo.com/v1/forecast?latitude={LAT}&longitude={LON}&current=temperature_2m,weather_code
     * 3. Maps WMO code:
     *      0 -> Clear
     *      1..3 -> Cloudy
     *      45, 48 -> Fog
     *      51..67 -> Rain
     *      71..77 -> Snow
     *      80..82 -> Showers
     *      95..99 -> Storm
     * 4. Dispatches exact BLE packet: WEATHER:<roundedTemp>C <condition> (e.g. WEATHER:28C Clear)
     */
    fun fetchLiveWeatherAndSend() {
        if (!manualWeatherOverride.isNullOrBlank()) {
            val pkt = "WEATHER:$manualWeatherOverride"
            onPacketReady(pkt)
            Log.i(tag, "Dispatched manual weather override: $pkt")
            return
        }

        scope.launch {
            try {
                val loc = resolveCoordinates()
                val lat = loc?.latitude ?: lastLat
                val lon = loc?.longitude ?: lastLon
                lastLat = lat
                lastLon = lon

                val endpoint = "https://api.open-meteo.com/v1/forecast?latitude=$lat&longitude=$lon&current=temperature_2m,weather_code"
                val url = URL(endpoint)
                val conn = withContext(Dispatchers.IO) {
                    (url.openConnection() as HttpURLConnection).apply {
                        requestMethod = "GET"
                        connectTimeout = 6000
                        readTimeout = 6000
                    }
                }

                if (conn.responseCode == HttpURLConnection.HTTP_OK) {
                    val reader = BufferedReader(InputStreamReader(conn.inputStream))
                    val response = reader.readText()
                    reader.close()

                    val json = JSONObject(response)
                    val current = json.getJSONObject("current")
                    val temp = current.getDouble("temperature_2m")
                    val roundedTemp = Math.round(temp).toInt()
                    val code = current.getInt("weather_code")
                    val condition = mapWmoCode(code)

                    val weatherStr = "${roundedTemp}C $condition"
                    _weatherState.value = weatherStr
                    val weatherPkt = "WEATHER:$weatherStr"
                    onPacketReady(weatherPkt)
                    Log.i(tag, "Fetched & sent live Open-Meteo weather ($lat, $lon): $weatherPkt")
                } else {
                    Log.w(tag, "Open-Meteo HTTP ${conn.responseCode}, keeping previous ${_weatherState.value}")
                }
            } catch (e: Exception) {
                Log.w(tag, "Open-Meteo weather fetch error: ${e.message}")
            }
        }
    }

    @SuppressLint("MissingPermission")
    private suspend fun resolveCoordinates(): Location? {
        val hasCoarse = ContextCompat.checkSelfPermission(
            context,
            Manifest.permission.ACCESS_COARSE_LOCATION
        ) == PackageManager.PERMISSION_GRANTED
        val hasFine = ContextCompat.checkSelfPermission(
            context,
            Manifest.permission.ACCESS_FINE_LOCATION
        ) == PackageManager.PERMISSION_GRANTED

        if (!hasCoarse && !hasFine) {
            Log.w(tag, "ACCESS_COARSE_LOCATION not granted; using last known default coordinates ($lastLat, $lastLon)")
            return null
        }

        return withTimeoutOrNull(3500) {
            suspendCancellableCoroutine { cont ->
                try {
                    fusedLocationClient.lastLocation
                        .addOnSuccessListener { loc ->
                            if (loc != null) {
                                cont.resume(loc)
                            } else {
                                val lm = context.getSystemService(Context.LOCATION_SERVICE) as? LocationManager
                                val fallback = lm?.getLastKnownLocation(LocationManager.NETWORK_PROVIDER)
                                    ?: lm?.getLastKnownLocation(LocationManager.GPS_PROVIDER)
                                cont.resume(fallback)
                            }
                        }
                        .addOnFailureListener {
                            val lm = context.getSystemService(Context.LOCATION_SERVICE) as? LocationManager
                            val fallback = lm?.getLastKnownLocation(LocationManager.NETWORK_PROVIDER)
                                ?: lm?.getLastKnownLocation(LocationManager.GPS_PROVIDER)
                            cont.resume(fallback)
                        }
                } catch (e: Exception) {
                    Log.w(tag, "Location query error: ${e.message}")
                    cont.resume(null)
                }
            }
        }
    }

    private fun mapWmoCode(code: Int): String {
        return when (code) {
            0 -> "Clear"
            in 1..3 -> "Cloudy"
            45, 48 -> "Fog"
            in 51..67 -> "Rain"
            in 71..77 -> "Snow"
            in 80..82 -> "Showers"
            in 95..99 -> "Storm"
            else -> "Clear"
        }
    }
}
