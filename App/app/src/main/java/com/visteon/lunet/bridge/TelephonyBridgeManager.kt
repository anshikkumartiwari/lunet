package com.visteon.lunet.bridge

import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.database.Cursor
import android.net.Uri
import android.os.Build
import android.provider.ContactsContract
import android.telecom.TelecomManager
import android.telephony.PhoneStateListener
import android.telephony.TelephonyCallback
import android.telephony.TelephonyManager
import android.util.Log
import androidx.core.content.ContextCompat
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

data class CallState(
    val isRinging: Boolean = false,
    val isActive: Boolean = false,
    val callerName: String = "",
    val callerNumber: String = ""
)

/**
 * Real Phone Telephony Bridge.
 * Detects incoming calls, resolves Contact Names, broadcasts to ESP32 cluster,
 * and handles CMD:CALL:ACCEPT and CMD:CALL:DECLINE via TelecomManager.
 */
class TelephonyBridgeManager(
    private val context: Context,
    private val onCallEvent: (String) -> Unit
) {
    private val tag = "TelephonyBridgeManager"
    private val telephonyManager = context.getSystemService(Context.TELEPHONY_SERVICE) as? TelephonyManager
    private val telecomManager = context.getSystemService(Context.TELECOM_SERVICE) as? TelecomManager

    private val _callState = MutableStateFlow(CallState())
    val callState: StateFlow<CallState> = _callState.asStateFlow()

    private var telephonyCallback: Any? = null
    private var phoneStateListener: PhoneStateListener? = null

    init {
        registerCallListener()
    }

    @SuppressLint("MissingPermission")
    private fun registerCallListener() {
        val tm = telephonyManager ?: return
        val hasPermission = ContextCompat.checkSelfPermission(
            context,
            android.Manifest.permission.READ_PHONE_STATE
        ) == PackageManager.PERMISSION_GRANTED

        if (!hasPermission) {
            Log.w(tag, "READ_PHONE_STATE not granted; call detection disabled until user grants it")
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
            }
        } catch (e: Exception) {
            Log.e(tag, "Failed to register call listener: ${e.message}")
        }
    }

    private fun handleStateChanged(state: Int, number: String?) {
        when (state) {
            TelephonyManager.CALL_STATE_RINGING -> {
                val contact = resolveContactName(number)
                _callState.value = CallState(
                    isRinging = true,
                    isActive = false,
                    callerName = contact,
                    callerNumber = number ?: ""
                )
                onCallEvent("CALL:INCOMING|$contact")
            }
            TelephonyManager.CALL_STATE_OFFHOOK -> {
                val contact = _callState.value.callerName.ifEmpty { resolveContactName(number) }
                _callState.value = CallState(
                    isRinging = false,
                    isActive = true,
                    callerName = contact,
                    callerNumber = number ?: ""
                )
                onCallEvent("CALL:ACTIVE|$contact")
            }
            TelephonyManager.CALL_STATE_IDLE -> {
                if (_callState.value.isRinging || _callState.value.isActive) {
                    _callState.value = CallState()
                    onCallEvent("CALL:ENDED")
                }
            }
        }
    }

    fun handleClusterCommand(cmd: String) {
        when (cmd) {
            "CMD:CALL:ACCEPT" -> acceptCall()
            "CMD:CALL:DECLINE" -> declineCall()
        }
    }

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
                Log.i(tag, "Accepted ringing call via TelecomManager")
            } catch (e: Exception) {
                Log.e(tag, "Failed to accept call: ${e.message}")
            }
        } else {
            Log.w(tag, "ANSWER_PHONE_CALLS permission required to answer call")
        }
    }

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
                    Log.i(tag, "Declined call via TelecomManager")
                } catch (e: Exception) {
                    Log.e(tag, "Failed to end call: ${e.message}")
                }
            }
        }
    }

    private fun resolveContactName(phoneNumber: String?): String {
        if (phoneNumber.isNullOrBlank()) return "Incoming Call"

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
                        return it.getString(nameIndex)
                    }
                }
            }
        } catch (e: Exception) {
            Log.w(tag, "Contact resolution error: ${e.message}")
        }
        return phoneNumber
    }
}
