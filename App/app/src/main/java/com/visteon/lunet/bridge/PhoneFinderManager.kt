package com.visteon.lunet.bridge

import android.content.Context
import android.media.AudioAttributes
import android.media.MediaPlayer
import android.media.RingtoneManager
import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * Working Anti-Loss "Ping Phone" Finder triggered from ESP32 cluster (CMD:PHONE:PING).
 * Uses high-priority USAGE_ALARM stream + pulsating vibration pattern + full UI overlay.
 */
class PhoneFinderManager(private val context: Context) {

    private val tag = "PhoneFinderManager"
    private val scope = CoroutineScope(Dispatchers.Main)

    private val _isAlarmActive = MutableStateFlow(false)
    val isAlarmActive: StateFlow<Boolean> = _isAlarmActive.asStateFlow()

    private var mediaPlayer: MediaPlayer? = null
    private var autoStopJob: Job? = null

    private val vibrator: Vibrator? get() {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val vm = context.getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as? VibratorManager
            vm?.defaultVibrator
        } else {
            @Suppress("DEPRECATION")
            context.getSystemService(Context.VIBRATOR_SERVICE) as? Vibrator
        }
    }

    fun triggerAlarm(reason: String = "CMD:PHONE:PING") {
        Log.i(tag, "Triggering emergency anti-loss alarm (source: $reason)")
        if (_isAlarmActive.value) return

        _isAlarmActive.value = true

        try {
            val alertUri = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_ALARM)
                ?: RingtoneManager.getDefaultUri(RingtoneManager.TYPE_RINGTONE)

            mediaPlayer?.release()
            mediaPlayer = MediaPlayer().apply {
                setDataSource(context, alertUri)
                setAudioAttributes(
                    AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_ALARM)
                        .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                        .build()
                )
                isLooping = true
                prepare()
                start()
            }
        } catch (e: Exception) {
            Log.e(tag, "Failed to start alarm audio: ${e.message}")
        }

        // Rhythmic vibration waveform
        try {
            val timings = longArrayOf(0, 500, 250, 500, 250, 800)
            val amplitudes = intArrayOf(0, 255, 0, 255, 0, 255)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                vibrator?.vibrate(VibrationEffect.createWaveform(timings, amplitudes, 0))
            } else {
                @Suppress("DEPRECATION")
                vibrator?.vibrate(timings, 0)
            }
        } catch (e: Exception) {
            Log.e(tag, "Failed to vibrate: ${e.message}")
        }

        // Auto-stop after 30 seconds if not cancelled
        autoStopJob?.cancel()
        autoStopJob = scope.launch {
            delay(30000)
            stopAlarm()
        }
    }

    fun stopAlarm() {
        if (!_isAlarmActive.value) return
        Log.i(tag, "Stopping anti-loss phone alarm")
        _isAlarmActive.value = false

        autoStopJob?.cancel()
        autoStopJob = null

        try {
            mediaPlayer?.stop()
            mediaPlayer?.release()
            mediaPlayer = null
        } catch (e: Exception) {
            Log.w(tag, "Error stopping media player: ${e.message}")
        }

        try {
            vibrator?.cancel()
        } catch (e: Exception) {
            Log.w(tag, "Error cancelling vibration: ${e.message}")
        }
    }
}
