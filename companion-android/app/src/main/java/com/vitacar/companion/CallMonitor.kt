package com.vitacar.companion

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.provider.ContactsContract
import android.telecom.TelecomManager
import android.telephony.PhoneStateListener
import android.telephony.TelephonyManager
import android.util.Log

/** Estado de llamadas -> Vita, y contestar / colgar desde la Vita. */
@Suppress("DEPRECATION")
class CallMonitor(private val context: Context) {

    private val telephony = context.getSystemService(Context.TELEPHONY_SERVICE) as TelephonyManager
    private val telecom = context.getSystemService(Context.TELECOM_SERVICE) as TelecomManager
    private var name = ""
    private var number = ""

    /* PhoneStateListener está obsoleto, pero es la única API que da el número
     * entrante sin ser la app de teléfono predeterminada. */
    private val listener = object : PhoneStateListener() {
        override fun onCallStateChanged(state: Int, phoneNumber: String?) {
            when (state) {
                TelephonyManager.CALL_STATE_RINGING -> {
                    number = phoneNumber.orEmpty()
                    name = lookupName(number)
                    send("ringing")
                }
                TelephonyManager.CALL_STATE_OFFHOOK -> send("active")
                TelephonyManager.CALL_STATE_IDLE -> {
                    send("idle")
                    name = ""
                    number = ""
                }
            }
        }
    }

    fun start() {
        if (!granted(Manifest.permission.READ_PHONE_STATE)) return
        telephony.listen(listener, PhoneStateListener.LISTEN_CALL_STATE)
    }

    fun stop() {
        telephony.listen(listener, PhoneStateListener.LISTEN_NONE)
    }

    @SuppressLint("MissingPermission")
    fun command(action: String) {
        if (!granted(Manifest.permission.ANSWER_PHONE_CALLS)) return
        try {
            when (action) {
                "answer" -> telecom.acceptRingingCall()
                "hangup" -> if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) telecom.endCall()
            }
        } catch (e: Exception) {
            Log.w("CallMonitor", "No se pudo ejecutar $action", e)
        }
    }

    private fun send(state: String) {
        VitaHub.updateCall(VitaHub.msg("call", "state" to state, "name" to name, "number" to number))
    }

    private fun lookupName(number: String): String {
        if (number.isEmpty() || !granted(Manifest.permission.READ_CONTACTS)) return ""
        val uri = Uri.withAppendedPath(ContactsContract.PhoneLookup.CONTENT_FILTER_URI, Uri.encode(number))
        return try {
            context.contentResolver.query(uri, arrayOf(ContactsContract.PhoneLookup.DISPLAY_NAME), null, null, null)
                ?.use { c -> if (c.moveToFirst()) c.getString(0) else "" } ?: ""
        } catch (_: Exception) {
            ""
        }
    }

    private fun granted(permission: String) =
        context.checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED
}
