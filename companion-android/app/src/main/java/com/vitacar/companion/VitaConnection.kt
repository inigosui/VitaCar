package com.vitacar.companion

import android.util.Log
import org.json.JSONObject
import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.IOException
import java.net.Socket
import java.util.concurrent.Executors
import java.util.concurrent.RejectedExecutionException
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread

/**
 * Una conexión con la Vita. Tramas: [u32 longitud BE][u8 tipo][datos], donde la
 * longitud incluye el byte de tipo. Ver docs/PROTOCOLO.md.
 */
class VitaConnection(
    private val socket: Socket,
    private val onMessage: (VitaConnection, JSONObject) -> Unit,
    private val onClosed: (VitaConnection) -> Unit,
) {
    companion object {
        const val FRAME_JSON = 1
        const val FRAME_ART = 2
        const val FRAME_TILE = 3
        private const val MAX_FRAME = 4 * 1024 * 1024
        /* La Vita envía un ping cada 3 s: si en 15 s no llega nada, está muerta. */
        private const val READ_TIMEOUT_MS = 15_000
        private const val TAG = "VitaConnection"
    }

    private val input = DataInputStream(BufferedInputStream(socket.getInputStream()))
    private val output = DataOutputStream(BufferedOutputStream(socket.getOutputStream()))
    private val closed = AtomicBoolean(false)
    /* Android prohíbe usar la red en el hilo principal, y los avisos de batería,
     * GPS o música llegan por ahí: todo envío pasa por este hilo, en orden. */
    private val writer = Executors.newSingleThreadExecutor { r -> Thread(r, "vita-writer").apply { isDaemon = true } }

    val remoteAddress: String = socket.inetAddress?.hostAddress ?: "?"

    fun start() {
        socket.soTimeout = READ_TIMEOUT_MS
        socket.tcpNoDelay = true
        thread(name = "vita-reader", isDaemon = true) { readLoop() }
    }

    private fun readLoop() {
        try {
            while (!closed.get()) {
                val length = input.readInt()
                if (length < 1 || length > MAX_FRAME) break
                val type = input.readUnsignedByte()
                val payload = ByteArray(length - 1)
                input.readFully(payload)
                if (type == FRAME_JSON) {
                    try {
                        onMessage(this, JSONObject(String(payload, Charsets.UTF_8)))
                    } catch (e: Exception) {
                        Log.w(TAG, "Mensaje inválido de la Vita", e)
                    }
                }
            }
        } catch (e: IOException) {
            Log.i(TAG, "Conexión cerrada: ${e.message}")
        } finally {
            close()
        }
    }

    fun sendJson(obj: JSONObject) = sendFrame(FRAME_JSON, obj.toString().toByteArray(Charsets.UTF_8))

    fun sendArt(id: Int, jpeg: ByteArray) {
        val payload = ByteArray(4 + jpeg.size)
        writeInt(payload, 0, id)
        System.arraycopy(jpeg, 0, payload, 4, jpeg.size)
        sendFrame(FRAME_ART, payload)
    }

    fun sendTile(z: Int, x: Int, y: Int, png: ByteArray) {
        val payload = ByteArray(9 + png.size)
        payload[0] = z.toByte()
        writeInt(payload, 1, x)
        writeInt(payload, 5, y)
        System.arraycopy(png, 0, payload, 9, png.size)
        sendFrame(FRAME_TILE, payload)
    }

    private fun sendFrame(type: Int, payload: ByteArray) {
        if (closed.get()) return
        try {
            writer.execute {
                try {
                    output.writeInt(payload.size + 1)
                    output.writeByte(type)
                    output.write(payload)
                    output.flush()
                } catch (e: IOException) {
                    close()
                }
            }
        } catch (_: RejectedExecutionException) {
            // La conexión se cerró entre la comprobación y el envío.
        }
    }

    fun close() {
        if (closed.compareAndSet(false, true)) {
            writer.shutdownNow()
            try {
                socket.close()
            } catch (_: IOException) {
            }
            onClosed(this)
        }
    }

    private fun writeInt(buf: ByteArray, offset: Int, value: Int) {
        buf[offset] = (value ushr 24).toByte()
        buf[offset + 1] = (value ushr 16).toByte()
        buf[offset + 2] = (value ushr 8).toByte()
        buf[offset + 3] = value.toByte()
    }
}
