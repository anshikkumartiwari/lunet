package com.visteon.lunet.bridge

import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.database.Cursor
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.net.Uri
import android.os.Build
import android.provider.ContactsContract
import android.telecom.TelecomManager
import android.telephony.PhoneStateListener
import android.telephony.TelephonyCallback
import android.telephony.TelephonyManager
import android.util.Log
import androidx.core.content.ContextCompat
import com.visteon.lunet.ble.LunetBleManager
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

data class CallState(
    val isRinging: Boolean = false,
    val isActive: Boolean = false,
    val isSpeakerOn: Boolean = false,
    val callerName: String = "",
    val callerNumber: String = ""
)

/**
 * Real Phone Telephony & Hands-Free Audio Bridge for Visteon Lunet OS 3.0.
 *
 * Responsibilities:
 * - Detects incoming calls via TelephonyCallback (API 31+) / PhoneStateListener (older)
 * - Dialer notification fallback for active call display names (Notification.CATEGORY_CALL)
 * - Resolves phone numbers to contact names via ContactsContract.PhoneLookup
 * - Dispatches exact BLE call states:
 *     Ringing: CALL:INCOMING|<callerName>
 *     Answered/Offhook: CALL:ACTIVE|<callerName>
 *     Ended: CALL:ENDED
 *     Speaker state: CALL:SPEAKER:ON / CALL:SPEAKER:OFF
 * - Executes ESP32 Cluster Commands:
 *     CMD:CALL:ACCEPT
 *     CMD:CALL:ACCEPT_SPEAKER
 *     CMD:CALL:SPEAKER:ON
 *     CMD:CALL:SPEAKER:OFF
 *     CMD:CALL:DECLINE
 */
class TelephonyBridgeManager(
    private val context: Context,
    private val onCallEvent: (String) -> Unit
) {
    private val tag = "TelephonyBridgeManager"
    private val telephonyManager = context.getSystemService(Context.TELEPHONY_SERVICE) as? TelephonyManager
    private val telecomManager = context.getSystemService(Context.TELECOM_SERVICE) as? TelecomManager
    private val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    private val _callState = MutableStateFlow(CallState())
    val callState: StateFlow<CallState> = _callState.asStateFlow()

    private val _isSpeakerOn = MutableStateFlow(false)
    val isSpeakerOn: StateFlow<Boolean> = _isSpeakerOn.asStateFlow()

    private var telephonyCallback: Any? = null
    private var phoneStateListener: PhoneStateListener? = null
    private var isListenerRegistered = false

    private var cachedCallerName: String = ""
    private var lastSentCallerName: String = ""

    init {
        registerCallListener()
        setupAudioDeviceListener()
    }

    @SuppressLint("MissingPermission")
    fun registerCallListener() {
        if (isListenerRegistered) return
        val tm = telephonyManager ?: return
        val hasPermission = ContextCompat.checkSelfPermission(
            context,
            android.Manifest.permission.READ_PHONE_STATE
        ) == PackageManager.PERMISSION_GRANTED

        if (!hasPermission) {
            Log.w(tag, "READ_PHONE_STATE not granted; call detection disabled until granted")
            return
        }

        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                val callback = object : TelephonyCallback(), TelephonyCallback.CallStateListener {
                    override fun onCallStateChanged(state: Int) {
                        handleStateChanged(state, null)
                    }
                }
                tm.registerTelephonyCallback(context.mainExecutor, callback)
                telephonyCallback = callback
                isListenerRegistered = true
                Log.i(tag, "Registered TelephonyCallback.CallStateListener (API 31+)")
            } else {
                @Suppress("DEPRECATION")
                val listener = object : PhoneStateListener() {
                    @Deprecated("Deprecated in Java")
                    override fun onCallStateChanged(state: Int, phoneNumber: String?) {
                        handleStateChanged(state, phoneNumber)
                    }
                }
                @Suppress("DEPRECATION")
                tm.listen(listener, PhoneStateListener.LISTEN_CALL_STATE)
                phoneStateListener = listener
                isListenerRegistered = true
                Log.i(tag, "Registered PhoneStateListener (API < 31)")
            }
        } catch (e: Exception) {
            Log.e(tag, "Failed to register call listener: ${e.message}")
        }
    }

    private fun setupAudioDeviceListener() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            try {
                audioManager?.addOnCommunicationDeviceChangedListener(context.mainExecutor) { device ->
                    val isSpeaker = device?.type == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER
                    if (_isSpeakerOn.value != isSpeaker) {
                        _isSpeakerOn.value = isSpeaker
                        _callState.value = _callState.value.copy(isSpeakerOn = isSpeaker)
                        onCallEvent(if (isSpeaker) "CALL:SPEAKER:ON" else "CALL:SPEAKER:OFF")
                        Log.i(tag, "System communication device changed: speaker=$isSpeaker")
                    }
                }
            } catch (e: Exception) {
                Log.w(tag, "Communication device listener unavailable: ${e.message}")
            }
        }
    }

    private fun handleStateChanged(state: Int, number: String?) {
        when (state) {
            TelephonyManager.CALL_STATE_RINGING -> {
                val name = if (cachedCallerName.isNotBlank() && cachedCallerName != "Unknown Caller") {
                    cachedCallerName
                } else {
                    formatCallerName(number)
                }
                cachedCallerName = name
                lastSentCallerName = name

                _callState.value = CallState(
                    isRinging = true,
                    isActive = false,
                    isSpeakerOn = _isSpeakerOn.value,
                    callerName = name,
                    callerNumber = number ?: ""
                )
                val pkt = "CALL:INCOMING|$name"
                Log.i(tag, "Dispatching: $pkt")
                onCallEvent(pkt)
            }
            TelephonyManager.CALL_STATE_OFFHOOK -> {
                val name = if (cachedCallerName.isNotBlank() && cachedCallerName != "Unknown Caller") {
                    cachedCallerName
                } else {
                    _callState.value.callerName.ifEmpty { formatCallerName(number) }
                }
                cachedCallerName = name
                lastSentCallerName = name

                _callState.value = CallState(
                    isRinging = false,
                    isActive = true,
                    isSpeakerOn = _isSpeakerOn.value,
                    callerName = name,
                    callerNumber = number ?: ""
                )
                val pkt = "CALL:ACTIVE|$name"
                Log.i(tag, "Dispatching: $pkt")
                onCallEvent(pkt)
            }
            TelephonyManager.CALL_STATE_IDLE -> {
                if (_callState.value.isRinging || _callState.value.isActive) {
                    _callState.value = CallState()
                    cachedCallerName = ""
                    lastSentCallerName = ""
                    setSpeakerphone(false, notify = false)
                    Log.i(tag, "Dispatching: CALL:ENDED")
                    onCallEvent("CALL:ENDED")
                }
            }
        }
    }

    /**
     * Fallback from LunetNotificationListenerService:
     * When the system dialer posts an active call notification (Notification.CATEGORY_CALL),
     * extract the caller name and re-send CALL:INCOMING if the name resolved slightly after ringing started.
     */
    fun onDialerNotificationDetected(nameOrNumber: String) {
        val resolvedName = formatCallerName(nameOrNumber)
        if (resolvedName.isBlank() || resolvedName == "Unknown Caller") return

        Log.i(tag, "Dialer notification resolved caller display name: '$resolvedName'")
        cachedCallerName = resolvedName

        if (_callState.value.isRinging) {
            // Re-send if the name resolves slightly after ringing starts or differs from previous
            if (lastSentCallerName != resolvedName) {
                lastSentCallerName = resolvedName
                _callState.value = _callState.value.copy(callerName = resolvedName)
                val pkt = "CALL:INCOMING|$resolvedName"
                Log.i(tag, "Re-sending updated ringing caller name: $pkt")
                onCallEvent(pkt)
            }
        } else if (_callState.value.isActive) {
            if (_callState.value.callerName.isBlank() ||
                _callState.value.callerName == "Unknown Caller" ||
                _callState.value.callerName == "Incoming Call" ||
                lastSentCallerName != resolvedName
            ) {
                lastSentCallerName = resolvedName
                _callState.value = _callState.value.copy(callerName = resolvedName)
                val pkt = "CALL:ACTIVE|$resolvedName"
                Log.i(tag, "Re-sending updated active caller name: $pkt")
                onCallEvent(pkt)
            }
        }
    }

    fun onDialerNotificationRemoved() {
        val tm = telephonyManager ?: return
        val state = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            tm.callState
        } else {
            @Suppress("DEPRECATION")
            tm.callState
        }
        if (state == TelephonyManager.CALL_STATE_IDLE && (_callState.value.isRinging || _callState.value.isActive)) {
            _callState.value = CallState()
            cachedCallerName = ""
            lastSentCallerName = ""
            setSpeakerphone(false, notify = false)
            onCallEvent("CALL:ENDED")
        }
    }

    /**
     * Executes all incoming ESP32 commands.
     */
    fun handleClusterCommand(cmd: String) {
        Log.i(tag, "Handling cluster call command: $cmd")
        when (cmd) {
            "CMD:CALL:ACCEPT" -> acceptCall()
            "CMD:CALL:ACCEPT_SPEAKER" -> acceptSpeaker()
            "CMD:CALL:SPEAKER:ON" -> setSpeakerphone(true)
            "CMD:CALL:SPEAKER:OFF" -> setSpeakerphone(false)
            "CMD:CALL:DECLINE" -> declineCall()
        }
    }

    /**
     * CMD:CALL:ACCEPT: Answer ringing call using TelecomManager.acceptRingingCall().
     */
    @SuppressLint("MissingPermission")
    fun acceptCall() {
        val tm = telecomManager ?: return
        val hasPermission = ContextCompat.checkSelfPermission(
            context,
            android.Manifest.permission.ANSWER_PHONE_CALLS
        ) == PackageManager.PERMISSION_GRANTED

        if (hasPermission && Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            try {
                tm.acceptRingingCall()
                Log.i(tag, "Accepted ringing call via TelecomManager.acceptRingingCall()")
            } catch (e: Exception) {
                Log.e(tag, "Failed to accept call: ${e.message}")
            }
        } else {
            Log.w(tag, "ANSWER_PHONE_CALLS permission required to accept ringing call")
        }
    }

    /**
     * CMD:CALL:ACCEPT_SPEAKER:
     * If ringing, call TelecomManager.acceptRingingCall() and turn loudspeaker ON after a 700 ms delay
     * (with a second check at 1500 ms so the system dialer doesn't reset audio routing).
     * If already off-hook, toggle loudspeaker.
     */
    fun acceptSpeaker() {
        if (_callState.value.isRinging) {
            acceptCall()
            scope.launch {
                delay(700)
                setSpeakerphone(true)
                Log.i(tag, "Turned loudspeaker ON at 700 ms delay")

                delay(800) // 1500 ms total elapsed
                if (_callState.value.isActive || isCallInProgress()) {
                    setSpeakerphone(true)
                    Log.i(tag, "Confirmed loudspeaker ON at 1500 ms delay")
                }
            }
        } else if (_callState.value.isActive || isCallInProgress()) {
            val currentlyOn = isSpeakerphoneActive()
            setSpeakerphone(!currentlyOn)
            Log.i(tag, "Off-hook toggled loudspeaker: new state = ${!currentlyOn}")
        }
    }

    fun toggleSpeaker() {
        val currentlyOn = isSpeakerphoneActive()
        setSpeakerphone(!currentlyOn)
    }

    /**
     * CMD:CALL:SPEAKER:ON / CMD:CALL:SPEAKER:OFF:
     * Route audio to/from built-in speaker using:
     * - AudioManager.setCommunicationDevice(AudioDeviceInfo.TYPE_BUILTIN_SPEAKER) / clearCommunicationDevice() on Android 12+ (API 31+)
     * - audioManager.isSpeakerphoneOn on older versions
     * Then send CALL:SPEAKER:ON or CALL:SPEAKER:OFF back over BLE.
     */
    fun setSpeakerphone(on: Boolean, notify: Boolean = true) {
        val am = audioManager ?: return
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                if (on) {
                    if (am.mode == AudioManager.MODE_NORMAL) {
                        am.mode = AudioManager.MODE_IN_COMMUNICATION
                    }
                    val speakerDevice = am.availableCommunicationDevices.firstOrNull {
                        it.type == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER
                    }
                    if (speakerDevice != null) {
                        val ok = am.setCommunicationDevice(speakerDevice)
                        Log.i(tag, "setCommunicationDevice(TYPE_BUILTIN_SPEAKER) = $ok")
                    }
                    @Suppress("DEPRECATION")
                    am.isSpeakerphoneOn = true
                } else {
                    am.clearCommunicationDevice()
                    @Suppress("DEPRECATION")
                    am.isSpeakerphoneOn = false
                    if (am.mode == AudioManager.MODE_IN_COMMUNICATION) {
                        am.mode = AudioManager.MODE_NORMAL
                    }
                    Log.i(tag, "clearCommunicationDevice() executed")
                }
            } else {
                @Suppress("DEPRECATION")
                am.isSpeakerphoneOn = on
            }

            _isSpeakerOn.value = on
            _callState.value = _callState.value.copy(isSpeakerOn = on)

            if (notify) {
                val pkt = if (on) "CALL:SPEAKER:ON" else "CALL:SPEAKER:OFF"
                Log.i(tag, "Dispatched speaker event: $pkt")
                onCallEvent(pkt)
            }
        } catch (e: Exception) {
            Log.e(tag, "Failed to set speakerphone: ${e.message}")
        }
    }

    fun isSpeakerphoneActive(): Boolean {
        val am = audioManager ?: return _isSpeakerOn.value
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            am.communicationDevice?.type == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER || _isSpeakerOn.value
        } else {
            @Suppress("DEPRECATION")
            am.isSpeakerphoneOn || _isSpeakerOn.value
        }
    }

    private fun isCallInProgress(): Boolean {
        val tm = telephonyManager ?: return false
        val state = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            tm.callState
        } else {
            @Suppress("DEPRECATION")
            tm.callState
        }
        return state == TelephonyManager.CALL_STATE_OFFHOOK || state == TelephonyManager.CALL_STATE_RINGING
    }

    /**
     * Keep existing CMD:CALL:DECLINE (TelecomManager.endCall()) working as-is.
     */
    @SuppressLint("MissingPermission")
    fun declineCall() {
        val tm = telecomManager ?: return
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            val hasPermission = ContextCompat.checkSelfPermission(
                context,
                android.Manifest.permission.ANSWER_PHONE_CALLS
            ) == PackageManager.PERMISSION_GRANTED
            if (hasPermission) {
                try {
                    tm.endCall()
                    Log.i(tag, "Declined call via TelecomManager.endCall()")
                } catch (e: Exception) {
                    Log.e(tag, "Failed to end call: ${e.message}")
                }
            }
        }
    }

    /**
     * Resolves phone numbers to contact names via ContactsContract.PhoneLookup,
     * falling back to the raw number or "Unknown Caller".
     */
    fun formatCallerName(rawInput: String?): String {
        if (rawInput.isNullOrBlank()) return "Unknown Caller"

        // If it already contains letters (e.g. "Mom", "Aditya Sharma"), it's an existing contact display name
        val hasLetters = rawInput.any { it.isLetter() }
        if (hasLetters) {
            return LunetBleManager.sanitizeField(rawInput).take(40)
        }

        // It is a phone number: resolve via ContactsContract.PhoneLookup
        val resolved = resolveContactName(rawInput)
        return LunetBleManager.sanitizeField(resolved.ifBlank { "Unknown Caller" }).take(40)
    }

    private fun resolveContactName(phoneNumber: String?): String {
        if (phoneNumber.isNullOrBlank()) return "Unknown Caller"

        val hasContactPerm = ContextCompat.checkSelfPermission(
            context,
            android.Manifest.permission.READ_CONTACTS
        ) == PackageManager.PERMISSION_GRANTED

        if (!hasContactPerm) return phoneNumber

        val uri = Uri.withAppendedPath(
            ContactsContract.PhoneLookup.CONTENT_FILTER_URI,
            Uri.encode(phoneNumber)
        )
        val projection = arrayOf(ContactsContract.PhoneLookup.DISPLAY_NAME)

        try {
            val cursor: Cursor? = context.contentResolver.query(uri, projection, null, null, null)
            cursor?.use {
                if (it.moveToFirst()) {
                    val nameIndex = it.getColumnIndex(ContactsContract.PhoneLookup.DISPLAY_NAME)
                    if (nameIndex != -1) {
                        val name = it.getString(nameIndex)
                        if (!name.isNullOrBlank()) {
                            return name
                        }
                    }
                }
            }
        } catch (e: Exception) {
            Log.w(tag, "ContactsContract query error for '$phoneNumber': ${e.message}")
        }
        return phoneNumber
    }
}
