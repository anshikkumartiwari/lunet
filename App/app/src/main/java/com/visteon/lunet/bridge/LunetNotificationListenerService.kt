package com.visteon.lunet.bridge

import android.app.Notification
import android.content.ComponentName
import android.content.Context
import android.content.pm.PackageManager
import android.media.session.MediaSessionManager
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import android.util.Log
import com.visteon.lunet.LunetApp

/**
 * Real Android Notification Bridge, Google Maps Navigation Extractor,
 * and Media Session Manager Bridge for Visteon Lunet OS 3.0.
 */
class LunetNotificationListenerService : NotificationListenerService() {

    private val tag = "LunetNotifListener"
    private var mediaSessionManager: MediaSessionManager? = null

    override fun onCreate() {
        super.onCreate()
        Log.i(tag, "LunetNotificationListenerService created")
        setupMediaSessionObserver()
    }

    override fun onListenerConnected() {
        super.onListenerConnected()
        Log.i(tag, "Notification Listener connected!")
        LunetApp.instance?.setNotificationListenerActive(true)
        updateActiveMediaSessions()
    }

    override fun onListenerDisconnected() {
        super.onListenerDisconnected()
        Log.w(tag, "Notification Listener disconnected!")
        LunetApp.instance?.setNotificationListenerActive(false)
    }

    private fun setupMediaSessionObserver() {
        try {
            mediaSessionManager = getSystemService(Context.MEDIA_SESSION_SERVICE) as? MediaSessionManager
            val component = ComponentName(this, LunetNotificationListenerService::class.java)
            mediaSessionManager?.addOnActiveSessionsChangedListener({ controllers ->
                val active = controllers?.firstOrNull()
                LunetApp.instance?.mediaBridge?.attachMediaController(active)
            }, component)
        } catch (e: Exception) {
            Log.e(tag, "Failed to register active media session listener: ${e.message}")
        }
    }

    private fun updateActiveMediaSessions() {
        try {
            val component = ComponentName(this, LunetNotificationListenerService::class.java)
            val controllers = mediaSessionManager?.getActiveSessions(component)
            LunetApp.instance?.mediaBridge?.attachMediaController(controllers?.firstOrNull())
        } catch (e: Exception) {
            Log.w(tag, "Error querying active sessions: ${e.message}")
        }
    }

    override fun onNotificationPosted(sbn: StatusBarNotification?) {
        if (sbn == null) return
        val pkg = sbn.packageName ?: return

        // 1. Google Maps Navigation Stream Extractor
        if (pkg == "com.google.android.apps.maps") {
            parseGoogleMapsNotification(sbn)
            return
        }

        // 2. Ignore non-clearable / ongoing notifications and self notifications
        if (!sbn.isClearable || pkg == packageName || pkg == "android") return

        val extras = sbn.notification?.extras ?: return
        val title = extras.getString(Notification.EXTRA_TITLE) ?: ""
        val text = extras.getCharSequence(Notification.EXTRA_TEXT)?.toString() ?: ""

        if (title.isBlank() && text.isBlank()) return

        // Resolve user-friendly app label
        val appName = try {
            val appInfo = packageManager.getApplicationInfo(pkg, 0)
            packageManager.getApplicationLabel(appInfo).toString()
        } catch (e: PackageManager.NameNotFoundException) {
            pkg.substringAfterLast('.').replaceFirstChar { it.uppercase() }
        }

        val messageBody = if (title.isNotBlank() && text.isNotBlank()) {
            "$title: $text"
        } else {
            title.ifBlank { text }
        }

        // Forward to Lunet App
        LunetApp.instance?.onNotificationCaptured(appName, messageBody)
    }

    private fun parseGoogleMapsNotification(sbn: StatusBarNotification) {
        val extras = sbn.notification?.extras ?: return
        val title = extras.getString(Notification.EXTRA_TITLE) ?: ""
        val text = extras.getCharSequence(Notification.EXTRA_TEXT)?.toString() ?: ""
        val subText = extras.getCharSequence(Notification.EXTRA_SUB_TEXT)?.toString() ?: ""

        val combined = "$title $text $subText".lowercase()

        // 1. Determine maneuver
        val maneuver = when {
            combined.contains("u-turn") || combined.contains("uturn") -> "UTURN"
            combined.contains("left") -> "LEFT"
            combined.contains("right") -> "RIGHT"
            else -> "STRAIGHT"
        }

        // 2. Extract distance in meters
        val distMeters = extractDistanceMeters(title.ifBlank { text })

        // 3. Extract street name
        val street = if (title.matches(Regex(".*\\b(m|km|ft|mi)\\b.*", RegexOption.IGNORE_CASE))) {
            text.ifBlank { "Next Turn" }
        } else {
            title.ifBlank { "Next Turn" }
        }.take(30)

        // 4. Extract ETA and remaining km
        val etaMin = extractEtaMinutes(subText.ifBlank { text })
        val remKm = extractRemainingKm(subText.ifBlank { text })

        val navPacket = "NAV:$maneuver|$distMeters|$street|$etaMin|$remKm"
        Log.i(tag, "Extracted Google Maps Nav: $navPacket")
        LunetApp.instance?.onNavigationUpdated(maneuver, distMeters, street, etaMin, remKm, navPacket)
    }

    private fun extractDistanceMeters(raw: String): Int {
        val kmMatch = Regex("([0-9]+(?:\\.[0-9]+)?)\\s*km", RegexOption.IGNORE_CASE).find(raw)
        if (kmMatch != null) {
            val km = kmMatch.groupValues[1].toFloatOrNull() ?: 1.0f
            return (km * 1000).toInt()
        }
        val mMatch = Regex("([0-9]+)\\s*m\\b", RegexOption.IGNORE_CASE).find(raw)
        if (mMatch != null) {
            return mMatch.groupValues[1].toIntOrNull() ?: 200
        }
        val ftMatch = Regex("([0-9]+)\\s*ft", RegexOption.IGNORE_CASE).find(raw)
        if (ftMatch != null) {
            val ft = ftMatch.groupValues[1].toIntOrNull() ?: 500
            return (ft * 0.3048).toInt()
        }
        return 250
    }

    private fun extractEtaMinutes(raw: String): Int {
        val minMatch = Regex("([0-9]+)\\s*(?:min|mins|m)", RegexOption.IGNORE_CASE).find(raw)
        return minMatch?.groupValues?.get(1)?.toIntOrNull() ?: 15
    }

    private fun extractRemainingKm(raw: String): Float {
        val kmMatch = Regex("([0-9]+(?:\\.[0-9]+)?)\\s*km", RegexOption.IGNORE_CASE).find(raw)
        return kmMatch?.groupValues?.get(1)?.toFloatOrNull() ?: 8.5f
    }
}
