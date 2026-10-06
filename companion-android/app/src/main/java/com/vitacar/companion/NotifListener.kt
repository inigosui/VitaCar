package com.vitacar.companion

import android.app.Notification
import android.app.RemoteInput
import android.content.ComponentName
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.Parcelable
import android.os.SystemClock
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import android.util.Log
import java.io.ByteArrayOutputStream
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.ConcurrentHashMap

/**
 * Con el permiso "Acceso a notificaciones" Android nos deja leer las
 * notificaciones de todas las apps y controlar sus sesiones multimedia.
 */
class NotifListener : NotificationListenerService() {

    companion object {
        @Volatile
        var instance: NotifListener? = null
            private set

        private const val TAG = "NotifListener"

        /** Apps de navegación: su notificación fija trae la próxima indicación. */
        private val NAV_PACKAGES = setOf(
            "com.google.android.apps.maps",
            "com.waze",
            "net.osmand",
            "net.osmand.plus",
            "com.sygic.aura",
            "com.here.app.maps",
            "app.organicmaps",
        )

        /** Avisos del propio sistema (teclado, USB, actualizaciones…), no mensajes. */
        private val SYSTEM_PACKAGES = setOf("android", "com.android.systemui", "com.android.settings")

        /** Categorías que no son mensajes para el usuario. */
        private val IGNORED_CATEGORIES = setOf(
            Notification.CATEGORY_CALL,
            Notification.CATEGORY_TRANSPORT,
            Notification.CATEGORY_PROGRESS,
            Notification.CATEGORY_SERVICE,
            Notification.CATEGORY_SYSTEM,
            Notification.CATEGORY_STATUS,
        )
    }

    private val replyActions = ConcurrentHashMap<String, Notification.Action>()
    /* Al responder, WhatsApp y otras apps vuelven a publicar la notificación con
     * nuestra respuesta: durante unos segundos se ignora para que no reaparezca. */
    private val recentlyDismissed = ConcurrentHashMap<String, Long>()
    private val main = Handler(Looper.getMainLooper())
    private val timeFormat = SimpleDateFormat("HH:mm", Locale.getDefault())
    private var sessions: MediaSessionManager? = null
    private var controller: MediaController? = null
    private var navKey: String? = null

    // ---------- Ciclo de vida ----------

    override fun onListenerConnected() {
        instance = this
        val msm = getSystemService(MEDIA_SESSION_SERVICE) as MediaSessionManager
        sessions = msm
        val component = ComponentName(this, NotifListener::class.java)
        try {
            msm.addOnActiveSessionsChangedListener(sessionsListener, component, main)
            pickController(msm.getActiveSessions(component))
        } catch (e: SecurityException) {
            Log.w(TAG, "Sin acceso a sesiones multimedia", e)
        }

        VitaHub.clearNotifs()
        try {
            activeNotifications?.sortedBy { it.postTime }?.forEach { handle(it) }
        } catch (e: Exception) {
            Log.w(TAG, "No se pudieron leer las notificaciones activas", e)
        }
    }

    override fun onListenerDisconnected() {
        sessions?.removeOnActiveSessionsChangedListener(sessionsListener)
        controller?.unregisterCallback(mediaCallback)
        controller = null
        instance = null
    }

    // ---------- Notificaciones ----------

    override fun onNotificationPosted(sbn: StatusBarNotification) = handle(sbn)

    override fun onNotificationRemoved(sbn: StatusBarNotification) {
        if (sbn.key == navKey) {
            navKey = null
            VitaHub.updateNav(VitaHub.msg("nav", "active" to false))
            return
        }
        replyActions.remove(sbn.key)
        VitaHub.removeNotif(sbn.key)
    }

    private fun handle(sbn: StatusBarNotification) {
        if (sbn.packageName == packageName) return
        val n = sbn.notification
        val extras = n.extras

        if (sbn.packageName in NAV_PACKAGES && (sbn.isOngoing || n.category == "navigation")) {
            handleNavigation(sbn)
            return
        }
        if (extras.containsKey(Notification.EXTRA_MEDIA_SESSION)) return
        if (n.flags and Notification.FLAG_GROUP_SUMMARY != 0) return
        if (sbn.isOngoing || !sbn.isClearable || n.category in IGNORED_CATEGORIES) return
        if (sbn.packageName in SYSTEM_PACKAGES) return
        recentlyDismissed[sbn.key]?.let { if (SystemClock.elapsedRealtime() - it < 8000) return }

        val title = (extras.getCharSequence(Notification.EXTRA_CONVERSATION_TITLE)
            ?: extras.getCharSequence(Notification.EXTRA_TITLE))?.toString().orEmpty()
        val text = lastMessage(extras)
            ?: (extras.getCharSequence(Notification.EXTRA_BIG_TEXT)
                ?: extras.getCharSequence(Notification.EXTRA_TEXT))?.toString().orEmpty()
        if (title.isBlank() && text.isBlank()) return

        val reply = n.actions?.firstOrNull { a ->
            (Build.VERSION.SDK_INT < Build.VERSION_CODES.P || a.semanticAction == Notification.Action.SEMANTIC_ACTION_REPLY ||
                a.semanticAction == Notification.Action.SEMANTIC_ACTION_NONE) &&
                a.remoteInputs?.any { it.allowFreeFormInput } == true
        }
        if (reply != null) replyActions[sbn.key] = reply else replyActions.remove(sbn.key)

        VitaHub.putNotif(
            sbn.key,
            VitaHub.msg(
                "notif",
                "id" to sbn.key,
                "app" to appLabel(sbn.packageName),
                "title" to title,
                "text" to text,
                "time" to timeFormat.format(Date(sbn.postTime)),
                "can_reply" to (reply != null),
            ),
        )
    }

    /** En chats (MessagingStyle) el texto útil es el último mensaje, con su remitente en grupos. */
    private fun lastMessage(extras: Bundle): String? {
        @Suppress("DEPRECATION")
        val messages: Array<Parcelable> = extras.getParcelableArray(Notification.EXTRA_MESSAGES) ?: return null
        val last = messages.lastOrNull() as? Bundle ?: return null
        val text = last.getCharSequence("text")?.toString() ?: return null
        val isGroup = extras.getBoolean(Notification.EXTRA_IS_GROUP_CONVERSATION, false)
        val sender = last.getCharSequence("sender")?.toString()
        return if (isGroup && !sender.isNullOrBlank()) "$sender: $text" else text
    }

    private fun handleNavigation(sbn: StatusBarNotification) {
        val extras = sbn.notification.extras
        val title = extras.getCharSequence(Notification.EXTRA_TITLE)?.toString().orEmpty()
        val text = (extras.getCharSequence(Notification.EXTRA_BIG_TEXT)
            ?: extras.getCharSequence(Notification.EXTRA_TEXT))?.toString().orEmpty()
        val sub = extras.getCharSequence(Notification.EXTRA_SUB_TEXT)?.toString().orEmpty()
        if (title.isBlank() && text.isBlank()) return
        navKey = sbn.key
        VitaHub.updateNav(VitaHub.msg("nav", "active" to true, "title" to title, "text" to text, "sub" to sub))
    }

    fun reply(key: String, text: String) {
        val action = replyActions[key] ?: return
        val inputs = action.remoteInputs ?: return
        recentlyDismissed[key] = SystemClock.elapsedRealtime()
        val intent = Intent()
        val results = Bundle()
        inputs.forEach { results.putCharSequence(it.resultKey, text) }
        RemoteInput.addResultsToIntent(inputs, intent, results)
        try {
            action.actionIntent.send(this, 0, intent)
        } catch (e: Exception) {
            Log.w(TAG, "No se pudo responder", e)
        }
    }

    fun dismiss(key: String) {
        recentlyDismissed[key] = SystemClock.elapsedRealtime()
        try {
            cancelNotification(key)
        } catch (_: Exception) {
        }
    }

    private fun appLabel(pkg: String): String = try {
        packageManager.getApplicationLabel(packageManager.getApplicationInfo(pkg, 0)).toString()
    } catch (_: PackageManager.NameNotFoundException) {
        pkg
    }

    // ---------- Música ----------

    fun activeController(): MediaController? = controller

    private val sessionsListener = MediaSessionManager.OnActiveSessionsChangedListener { list ->
        pickController(list ?: emptyList())
    }

    private val mediaCallback = object : MediaController.Callback() {
        override fun onMetadataChanged(metadata: MediaMetadata?) = sendMedia()
        override fun onPlaybackStateChanged(state: PlaybackState?) = sendMedia()
        override fun onSessionDestroyed() {
            controller = null
            sendMedia()
        }
    }

    /** Se queda con la sesión que esté sonando; si ninguna, con la más reciente. */
    private fun pickController(list: List<MediaController>) {
        val best = list.firstOrNull { it.playbackState?.state == PlaybackState.STATE_PLAYING } ?: list.firstOrNull()
        if (best?.sessionToken == controller?.sessionToken) {
            sendMedia()
            return
        }
        controller?.unregisterCallback(mediaCallback)
        controller = best
        best?.registerCallback(mediaCallback, main)
        sendMedia()
    }

    private fun sendMedia() {
        val c = controller
        val md = c?.metadata
        if (c == null || md == null) {
            VitaHub.updateMedia(VitaHub.msg("media", "active" to false), 0, null)
            return
        }
        val state = c.playbackState
        val playing = state?.state == PlaybackState.STATE_PLAYING
        var position = state?.position ?: 0L
        if (playing && state.lastPositionUpdateTime > 0) {
            position += ((SystemClock.elapsedRealtime() - state.lastPositionUpdateTime) * state.playbackSpeed).toLong()
        }

        val title = md.getString(MediaMetadata.METADATA_KEY_TITLE).orEmpty()
        val artist = md.getString(MediaMetadata.METADATA_KEY_ARTIST)
            ?: md.getString(MediaMetadata.METADATA_KEY_ALBUM_ARTIST).orEmpty()
        val album = md.getString(MediaMetadata.METADATA_KEY_ALBUM).orEmpty()
        val bitmap = md.getBitmap(MediaMetadata.METADATA_KEY_ALBUM_ART)
            ?: md.getBitmap(MediaMetadata.METADATA_KEY_ART)
            ?: md.getBitmap(MediaMetadata.METADATA_KEY_DISPLAY_ICON)
        // Id estable por canción: la portada solo se reenvía cuando cambia.
        val artId = if (bitmap != null) ("$title|$artist|$album".hashCode() and 0x7FFFFFFF).coerceAtLeast(1) else 0

        VitaHub.updateMedia(
            VitaHub.msg(
                "media",
                "active" to true,
                "app" to appLabel(c.packageName),
                "title" to title,
                "artist" to artist,
                "dur" to md.getLong(MediaMetadata.METADATA_KEY_DURATION),
                "pos" to position,
                "playing" to playing,
                "art_id" to artId,
            ),
            artId,
            bitmap?.let(::toJpeg),
        )
    }

    private fun toJpeg(bitmap: Bitmap): ByteArray {
        val scaled = Bitmap.createScaledBitmap(bitmap, 300, 300, true)
        return ByteArrayOutputStream().use { out ->
            scaled.compress(Bitmap.CompressFormat.JPEG, 82, out)
            out.toByteArray()
        }
    }
}
