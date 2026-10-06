package com.vitacar.companion

import android.content.Context
import android.media.AudioManager
import android.media.session.MediaController
import android.media.session.PlaybackState
import android.os.SystemClock
import android.view.KeyEvent

/** Órdenes de reproducción que llegan desde la Vita. */
object MediaCommands {

    fun send(context: Context, controller: MediaController?, action: String) {
        if (controller != null) {
            val controls = controller.transportControls
            when (action) {
                "play_pause" ->
                    if (controller.playbackState?.state == PlaybackState.STATE_PLAYING) controls.pause() else controls.play()
                "next" -> controls.skipToNext()
                "prev" -> controls.skipToPrevious()
            }
            return
        }
        // Sin sesión activa (p. ej. Spotify cerrado): tecla multimedia, que reanuda el último reproductor.
        val code = when (action) {
            "play_pause" -> KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE
            "next" -> KeyEvent.KEYCODE_MEDIA_NEXT
            "prev" -> KeyEvent.KEYCODE_MEDIA_PREVIOUS
            else -> return
        }
        val audio = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        val now = SystemClock.uptimeMillis()
        audio.dispatchMediaKeyEvent(KeyEvent(now, now, KeyEvent.ACTION_DOWN, code, 0))
        audio.dispatchMediaKeyEvent(KeyEvent(now, now, KeyEvent.ACTION_UP, code, 0))
    }
}
