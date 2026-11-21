package com.visteon.lunet.bridge

import android.content.Context
import android.database.ContentObserver
import android.media.AudioManager
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSession
import android.media.session.PlaybackState
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.provider.Settings
import android.util.Log
import android.view.KeyEvent
import com.visteon.lunet.ble.LunetBleManager
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

data class MediaPlaybackInfo(
    val title: String = "No Media Playing",
    val artist: String = "Lunet Audio Bridge",
    val isPlaying: Boolean = false,
    val progressPercent: Int = 0,
    val volumePercent: Int = 50,
    val activeEqProfile: String = "STUDIO",
    val durationMs: Long = 0L,
    val positionMs: Long = 0L
)

/**
 * Two-way Media Controller & Audio Manager bridge for Visteon Lunet OS 3.0.
 * Features:
 * - Active session listener with Notification EXTRA_MEDIA_SESSION token fallback
 * - Extrapolated real-time progress calculations & 3-second live sync loop
 * - ContentObserver for physical volume key sync
 * - Complete transport controls + seek + EQ presets
 */
class MediaBridgeManager(private val context: Context) {

    private val tag = "MediaBridgeManager"
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    private val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager

    private var activeController: MediaController? = null
    private var progressLoopJob: Job? = null

    private var lastRecordedPosition: Long = 0L
    private var lastRecordedUpdateTime: Long = 0L
    private var lastPlaybackSpeed: Float = 1.0f

    private val _playbackInfo = MutableStateFlow(MediaPlaybackInfo(volumePercent = getCurrentVolumePercent()))
    val playbackInfo: StateFlow<MediaPlaybackInfo> = _playbackInfo.asStateFlow()

    private val controllerCallback = object : MediaController.Callback() {
        override fun onPlaybackStateChanged(state: PlaybackState?) {
            updatePlaybackState(state)
        }

        override fun onMetadataChanged(metadata: MediaMetadata?) {
            updateMetadata(metadata)
        }
    }

    private val volumeObserver = object : ContentObserver(Handler(Looper.getMainLooper())) {
        override fun onChange(selfChange: Boolean, uri: Uri?) {
            super.onChange(selfChange, uri)
            val vol = getCurrentVolumePercent()
            if (_playbackInfo.value.volumePercent != vol) {
                _playbackInfo.value = _playbackInfo.value.copy(volumePercent = vol)
            }
        }
    }

    init {
        try {
            context.contentResolver.registerContentObserver(
                Settings.System.CONTENT_URI,
                true,
                volumeObserver
            )
        } catch (e: Exception) {
            Log.w(tag, "Volume ContentObserver init warning: ${e.message}")
        }
    }

    fun attachMediaController(controller: MediaController?) {
        if (controller == null) return
        if (activeController?.sessionToken == controller.sessionToken && activeController != null) {
            updatePlaybackState(activeController?.playbackState)
            return
        }

        try {
            activeController?.unregisterCallback(controllerCallback)
        } catch (e: Exception) {
            Log.w(tag, "Unregister callback exception: ${e.message}")
        }

        activeController = controller
        try {
            activeController?.registerCallback(controllerCallback)
        } catch (e: Exception) {
            Log.w(tag, "Register callback exception: ${e.message}")
        }

        updateMetadata(activeController?.metadata)
        updatePlaybackState(activeController?.playbackState)
    }

    fun attachSessionToken(token: MediaSession.Token?) {
        if (token == null) return
        if (activeController?.sessionToken == token && _playbackInfo.value.isPlaying) return

        try {
            val controller = MediaController(context, token)
            // If current controller is not playing or null, adopt this new token
            if (activeController == null || activeController?.playbackState?.state != PlaybackState.STATE_PLAYING) {
                attachMediaController(controller)
            }
        } catch (e: Exception) {
            Log.w(tag, "Failed to create MediaController from token: ${e.message}")
        }
    }

    fun updateFromNotificationExtras(title: String?, text: String?) {
        if (title.isNullOrBlank() && text.isNullOrBlank()) return
        if (_playbackInfo.value.title == "No Media Playing" || !_playbackInfo.value.isPlaying) {
            val cleanTitle = LunetBleManager.sanitizeField(title ?: "Unknown Track").take(36)
            val cleanArtist = LunetBleManager.sanitizeField(text ?: "Unknown Artist").take(36)
            _playbackInfo.value = _playbackInfo.value.copy(
                title = cleanTitle,
                artist = cleanArtist
            )
        }
    }

    private fun updateMetadata(metadata: MediaMetadata?) {
        val title = metadata?.getString(MediaMetadata.METADATA_KEY_TITLE)
            ?: metadata?.getString(MediaMetadata.METADATA_KEY_DISPLAY_TITLE)
            ?: _playbackInfo.value.title
        val artist = metadata?.getString(MediaMetadata.METADATA_KEY_ARTIST)
            ?: metadata?.getString(MediaMetadata.METADATA_KEY_ALBUM_ARTIST)
            ?: metadata?.getString(MediaMetadata.METADATA_KEY_AUTHOR)
            ?: _playbackInfo.value.artist
        val duration = metadata?.getLong(MediaMetadata.METADATA_KEY_DURATION) ?: 0L

        val cleanTitle = LunetBleManager.sanitizeField(title).take(36)
        val cleanArtist = LunetBleManager.sanitizeField(artist).take(36)

        _playbackInfo.value = _playbackInfo.value.copy(
            title = cleanTitle.ifBlank { "Unknown Track" },
            artist = cleanArtist.ifBlank { "Unknown Artist" },
            durationMs = duration
        )
    }

    private fun updatePlaybackState(state: PlaybackState?) {
        val isPlaying = state?.state == PlaybackState.STATE_PLAYING
        val duration = activeController?.metadata?.getLong(MediaMetadata.METADATA_KEY_DURATION)
            ?: _playbackInfo.value.durationMs

        lastRecordedPosition = state?.position ?: 0L
        lastRecordedUpdateTime = state?.lastPositionUpdateTime.takeIf { (it ?: 0L) > 0 }
            ?: SystemClock.elapsedRealtime()
        lastPlaybackSpeed = state?.playbackSpeed ?: 1.0f

        val progress = if (duration > 0) {
            ((lastRecordedPosition * 100) / duration).toInt().coerceIn(0, 100)
        } else {
            0
        }

        _playbackInfo.value = _playbackInfo.value.copy(
            isPlaying = isPlaying,
            progressPercent = progress,
            positionMs = lastRecordedPosition,
            durationMs = duration,
            volumePercent = getCurrentVolumePercent()
        )

        if (isPlaying) {
            startProgressLoop()
        } else {
            stopProgressLoop()
        }
    }

    private fun startProgressLoop() {
        progressLoopJob?.cancel()
        progressLoopJob = scope.launch {
            while (isActive && _playbackInfo.value.isPlaying) {
                delay(3000)
                recalculateExtrapolatedProgress()
            }
        }
    }

    private fun stopProgressLoop() {
        progressLoopJob?.cancel()
        progressLoopJob = null
    }

    private fun recalculateExtrapolatedProgress() {
        val duration = _playbackInfo.value.durationMs
        if (duration <= 0) return

        val now = SystemClock.elapsedRealtime()
        val elapsed = (now - lastRecordedUpdateTime).coerceAtLeast(0)
        val currentPos = (lastRecordedPosition + (elapsed * lastPlaybackSpeed).toLong()).coerceIn(0, duration)
        val pct = ((currentPos * 100) / duration).toInt().coerceIn(0, 100)

        _playbackInfo.value = _playbackInfo.value.copy(
            progressPercent = pct,
            positionMs = currentPos,
            volumePercent = getCurrentVolumePercent()
        )
    }

    fun getCurrentVolumePercent(): Int {
        val current = audioManager.getStreamVolume(AudioManager.STREAM_MUSIC)
        val max = audioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC)
        return if (max > 0) ((current * 100) / max).coerceIn(0, 100) else 50
    }

    fun formatMediaPacket(): String {
        val info = _playbackInfo.value
        val playState = if (info.isPlaying) "PLAY" else "PAUSE"
        val vol = getCurrentVolumePercent()
        return "MEDIA:${info.title}|${info.artist}|$playState|${info.progressPercent}|$vol"
    }

    fun handleClusterCommand(cmd: String) {
        Log.d(tag, "Handling cluster media command: $cmd")
        when {
            cmd == "CMD:MEDIA:PLAY" -> play()
            cmd == "CMD:MEDIA:PAUSE" -> pause()
            cmd == "CMD:MEDIA:NEXT" -> next()
            cmd == "CMD:MEDIA:PREV" -> prev()
            cmd.startsWith("CMD:MEDIA:SEEK:") -> {
                val pct = cmd.substringAfter("CMD:MEDIA:SEEK:").toIntOrNull() ?: 0
                seekTo(pct)
            }
            cmd.startsWith("CMD:VOL:") -> {
                val sub = cmd.substringAfter("CMD:VOL:")
                when (sub) {
                    "MUTE" -> setMute(true)
                    "UNMUTE" -> setMute(false)
                    else -> {
                        val vol = sub.toIntOrNull() ?: 50
                        setVolumePercent(vol)
                    }
                }
            }
            cmd.startsWith("CMD:EQ:") -> {
                val preset = cmd.substringAfter("CMD:EQ:")
                setEqPreset(preset)
            }
        }
    }

    fun play() {
        val controls = activeController?.transportControls
        if (controls != null) {
            controls.play()
        } else {
            dispatchMediaKeyEvent(KeyEvent.KEYCODE_MEDIA_PLAY)
        }
        _playbackInfo.value = _playbackInfo.value.copy(isPlaying = true)
        startProgressLoop()
    }

    fun pause() {
        val controls = activeController?.transportControls
        if (controls != null) {
            controls.pause()
        } else {
            dispatchMediaKeyEvent(KeyEvent.KEYCODE_MEDIA_PAUSE)
        }
        _playbackInfo.value = _playbackInfo.value.copy(isPlaying = false)
        stopProgressLoop()
    }

    fun next() {
        val controls = activeController?.transportControls
        if (controls != null) {
            controls.skipToNext()
        } else {
            dispatchMediaKeyEvent(KeyEvent.KEYCODE_MEDIA_NEXT)
        }
    }

    fun prev() {
        val controls = activeController?.transportControls
        if (controls != null) {
            controls.skipToPrevious()
        } else {
            dispatchMediaKeyEvent(KeyEvent.KEYCODE_MEDIA_PREVIOUS)
        }
    }

    fun seekTo(percent: Int) {
        val duration = _playbackInfo.value.durationMs
        if (duration > 0) {
            val targetPos = (duration * percent.coerceIn(0, 100)) / 100
            activeController?.transportControls?.seekTo(targetPos)
            lastRecordedPosition = targetPos
            lastRecordedUpdateTime = SystemClock.elapsedRealtime()
        }
        _playbackInfo.value = _playbackInfo.value.copy(progressPercent = percent.coerceIn(0, 100))
    }

    fun setVolumePercent(pct: Int) {
        val max = audioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC)
        val target = ((max * pct.coerceIn(0, 100)) / 100)
        audioManager.setStreamVolume(AudioManager.STREAM_MUSIC, target, 0)
        _playbackInfo.value = _playbackInfo.value.copy(volumePercent = pct.coerceIn(0, 100))
    }

    fun setMute(mute: Boolean) {
        val flag = if (mute) AudioManager.ADJUST_MUTE else AudioManager.ADJUST_UNMUTE
        audioManager.adjustStreamVolume(AudioManager.STREAM_MUSIC, flag, 0)
    }

    fun setEqPreset(preset: String) {
        Log.i(tag, "Applied EQ profile: $preset")
        _playbackInfo.value = _playbackInfo.value.copy(activeEqProfile = preset)
    }

    private fun dispatchMediaKeyEvent(keyCode: Int) {
        audioManager.dispatchMediaKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, keyCode))
        audioManager.dispatchMediaKeyEvent(KeyEvent(KeyEvent.ACTION_UP, keyCode))
    }
}
