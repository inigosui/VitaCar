package com.vitacar.companion

import android.annotation.SuppressLint
import android.annotation.TargetApi
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioPlaybackCaptureConfiguration
import android.media.AudioRecord
import android.media.projection.MediaProjection
import android.os.Build
import android.util.Log
import kotlin.concurrent.thread

/**
 * Captura el sonido que reproducen las apps (Android 10+) y lo envía a la Vita como PCM
 * de 16 bits, estéreo, 48 kHz (unos 1,5 Mbit/s). Android no deja capturar las apps que
 * lo bloquean (Spotify, por ejemplo), ni las llamadas ni la voz de la navegación.
 */
@TargetApi(Build.VERSION_CODES.Q)
class AudioStreamer(private val projection: MediaProjection) {

    companion object {
        const val RATE = 48_000
        const val CHANNELS = 2
        private const val CHUNK = RATE / 50 * CHANNELS * 2      // 20 ms
        private const val TAG = "AudioStreamer"
    }

    @Volatile
    private var running = false
    private var worker: Thread? = null

    /** Necesita el permiso RECORD_AUDIO: lo comprueba quien lo crea. */
    @SuppressLint("MissingPermission")
    fun start(): Boolean {
        val config = AudioPlaybackCaptureConfiguration.Builder(projection)
            .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
            .addMatchingUsage(AudioAttributes.USAGE_GAME)
            .addMatchingUsage(AudioAttributes.USAGE_UNKNOWN)
            .build()
        val format = AudioFormat.Builder()
            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
            .setSampleRate(RATE)
            .setChannelMask(AudioFormat.CHANNEL_IN_STEREO)
            .build()
        val minBuf = AudioRecord.getMinBufferSize(RATE, AudioFormat.CHANNEL_IN_STEREO, AudioFormat.ENCODING_PCM_16BIT)
        val record = try {
            AudioRecord.Builder()
                .setAudioFormat(format)
                .setBufferSizeInBytes(maxOf(minBuf, CHUNK * 8))
                .setAudioPlaybackCaptureConfig(config)
                .build()
        } catch (e: Exception) {
            Log.e(TAG, "No se pudo crear la captura", e)
            return false
        }
        if (record.state != AudioRecord.STATE_INITIALIZED) {
            record.release()
            return false
        }

        running = true
        worker = thread(name = "audio-capture", isDaemon = true) {
            val buf = ByteArray(CHUNK)
            try {
                record.startRecording()
                while (running) {
                    var n = 0
                    while (n < CHUNK && running) {
                        val r = record.read(buf, n, CHUNK - n)
                        if (r < 0) {
                            Log.w(TAG, "Error de lectura $r")
                            running = false
                            break
                        }
                        n += r
                    }
                    // Se lee siempre, haya o no Vita, para que la captura no se atasque.
                    if (n == CHUNK) VitaHub.connection?.sendAudio(buf.copyOf())
                }
            } catch (e: Exception) {
                Log.e(TAG, "Captura detenida", e)
            } finally {
                try {
                    record.stop()
                } catch (_: IllegalStateException) {
                }
                record.release()
            }
        }
        return true
    }

    fun stop() {
        running = false
        worker?.join(500)
        worker = null
    }
}
