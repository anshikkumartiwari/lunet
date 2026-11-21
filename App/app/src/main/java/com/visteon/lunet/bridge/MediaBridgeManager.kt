package com.visteon.lunet.bridge

import android.content.Context
import android.media.AudioManager
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.PlaybackState
import android.util.Log
import android.view.KeyEvent
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

data class MediaPlaybackInfo(
    val title: String = "Lunet Audio",
    val artist: String = "Visteon Acoustic",
    val isPlaying: Boolean = false,
    val progressPercent: Int = 0,
    val volumePercent: Int = 50,
    val activeEqProfile: String = "STUDIO"
)

/**
 * Two-way Media Controller & Audio Manager bridge.
 * Bridges phone media sessions (Spotify, YT Music, etc.) to Lunet OS 3.0 cluster.
 */
class MediaBridgeManager(private val context: Context) {

    private val tag = "MediaBridgeManager"
    private val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager

    private var activeController: MediaController? = null

    private val _playbackInfo = MutableStateFlow(MediaPlaybackInfo())
    val playbackInfo: StateFlow<MediaPlaybackInfo> = _playbackInfo.asStateFlow()

    private val controllerCallback = object : MediaController.Callback() {
        override fun onPlaybackStateChanged(state: PlaybackState?) {
            updatePlaybackState(state)
        }

        override fun onMetadataChanged(metadata: MediaMetadata?) {
            updateMetadata(metadata)
        }
    }

    fun attachMediaController(controller: MediaController?) {
        activeController?.unregisterCallback(controllerCallback)
        activeController = controller
        activeController?.registerCallback(controllerCallback)

        updateMetadata(activeController?.metadata)
        updatePlaybackState(activeController?.playbackState)
    }

    private fun updateMetadata(metadata: MediaMetadata?) {
        val title = metadata?.getString(MediaMetadata.METADATA_KEY_TITLE)
            ?: metadata?.getString(MediaMetadata.METADATA_KEY_DISPLAY_TITLE)
            ?: _playbackInfo.value.title
        val artist = metadata?.getString(MediaMetadata.METADATA_KEY_ARTIST)
            ?: metadata?.getString(MediaMetadata.METADATA_KEY_ALBUM_ARTIST)
            ?: _playbackInfo.value.artist

        _playbackInfo.value = _playbackInfo.value.copy(
            title = title.take(40),
            artist = artist.take(40)
        )
    }

    private fun updatePlaybackState(state: PlaybackState?) {
        val isPlaying = state?.state == PlaybackState.STATE_PLAYING
        val duration = activeController?.metadata?.getLong(MediaMetadata.METADATA_KEY_DURATION) ?: 0L
        val position = state?.position ?: 0L
        val progress = if (duration > 0) ((position * 100) / duration).toInt().coerceIn(0, 100) else 0

        _playbackInfo.value = _playbackInfo.value.copy(
            isPlaying = isPlaying,
            progressPercent = progress,
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

    // Incoming cluster commands handling
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
                if (sub == "MUTE") {
                    setMute(true)
                } else if (sub == "UNMUTE") {
                    setMute(false)
                } else {
                    val vol = sub.toIntOrNull() ?: 50
                    setVolumePercent(vol)
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
    }

    fun pause() {
        val controls = activeController?.transportControls
        if (controls != null) {
            controls.pause()
        } else {
            dispatchMediaKeyEvent(KeyEvent.KEYCODE_MEDIA_PAUSE)
        }
        _playbackInfo.value = _playbackInfo.value.copy(isPlaying = false)
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
        val duration = activeController?.metadata?.getLong(MediaMetadata.METADATA_KEY_DURATION) ?: 0L
        if (duration > 0) {
            val targetPos = (duration * percent.coerceIn(0, 100)) / 100
            activeController?.transportControls?.seekTo(targetPos)
        }
        _playbackInfo.value = _playbackInfo.value.copy(progressPercent = percent.coerceIn(0, 100))
    }

    fun setVolumePercent(pct: Int) {
        val max = audioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC)
        val target = ((max * pct.coerceIn(0, 100)) / 100)
        audioManager.setStreamVolume(AudioManager.STREAM_MUSIC, target, 0)
        _playbackInfo.value = _playbackInfo.value.copy(volumePercent = pct)
    }

    fun setMute(mute: Boolean) {
        val flag = if (mute) AudioManager.ADJUST_MUTE else AudioManager.ADJUST_UNMUTE
        audioManager.adjustStreamVolume(AudioManager.STREAM_MUSIC, flag, 0)
    }

    fun setEqPreset(preset: String) {
        Log.i(tag, "Lunet OS EQ preset applied: $preset")
        _playbackInfo.value = _playbackInfo.value.copy(activeEqProfile = preset)
    }

    private fun dispatchMediaKeyEvent(keyCode: Int) {
        audioManager.dispatchMediaKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, keyCode))
        audioManager.dispatchMediaKeyEvent(KeyEvent(KeyEvent.ACTION_UP, keyCode))
    }
}
