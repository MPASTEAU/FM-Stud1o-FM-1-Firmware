package fr.felucca.midihub

import android.annotation.SuppressLint
import android.content.Context
import android.media.AudioDeviceInfo
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioRecord
import android.media.MediaRecorder
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder

/*
 * A take at its best: 48 kHz, 32-bit float, stereo. Two inputs, each asked for by name (the phone otherwise takes the
 * FM-1 for its microphone once it is plugged in, the FM-1 being a USB audio input):
 *   MICROS  the phone's own (both: the stereo video source, CAMCORDER, on the built-in microphone)
 *   FM-1    what the FM-1 plays, over USB (resampling it)
 * A take is kept as it came (WAV, 32-bit float) next to its cleaned copy (getExternalFilesDir("samples")).
 */
class Recorder(private val ctx: Context) {
    companion object {
        const val RATE = 48000
        const val MAX_S = 60                                  // a take's longest, seconds
    }

    var fromFm1 = false
    @Volatile private var going = false
    private var thread: Thread? = null
    private val buf = FloatArray(RATE * 2 * MAX_S)
    @Volatile private var len = 0                            // floats (interleaved L R)
    @Volatile var level = 0f                                 // the last block's peak (the meter)
    var device = ""                                          // the input it records from

    val recording get() = going
    val seconds get() = len / 2f / RATE

    private fun input(): AudioDeviceInfo? {
        val all = ctx.getSystemService(AudioManager::class.java).getDevices(AudioManager.GET_DEVICES_INPUTS)
        return if (fromFm1) all.firstOrNull { it.type == AudioDeviceInfo.TYPE_USB_DEVICE || it.type == AudioDeviceInfo.TYPE_USB_HEADSET }
        else all.firstOrNull { it.type == AudioDeviceInfo.TYPE_BUILTIN_MIC }
    }

    @SuppressLint("MissingPermission")
    fun start(): String? {
        val dev = input() ?: return if (fromFm1) "FM-1 introuvable comme entrée audio USB" else "micro du téléphone introuvable"
        val fmt = AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_FLOAT).setSampleRate(RATE)
            .setChannelMask(AudioFormat.CHANNEL_IN_STEREO).build()
        val min = AudioRecord.getMinBufferSize(RATE, AudioFormat.CHANNEL_IN_STEREO, AudioFormat.ENCODING_PCM_FLOAT)
        val src = if (fromFm1) MediaRecorder.AudioSource.UNPROCESSED else MediaRecorder.AudioSource.CAMCORDER
        val rec = try {
            AudioRecord.Builder().setAudioSource(src).setAudioFormat(fmt).setBufferSizeInBytes(maxOf(min, RATE * 8 / 5)).build()
        } catch (e: Exception) {
            return "entrée indisponible : ${e.message}"
        }
        if (rec.state != AudioRecord.STATE_INITIALIZED) {
            rec.release()
            return "entrée indisponible"
        }
        rec.preferredDevice = dev
        len = 0
        going = true
        rec.startRecording()
        device = rec.routedDevice?.let { "${it.productName} (${if (it.type == AudioDeviceInfo.TYPE_BUILTIN_MIC) "téléphone" else "USB"})" } ?: "?"
        thread = Thread {
            val blk = FloatArray(RATE / 50 * 2)
            while (going) {
                val n = rec.read(blk, 0, blk.size, AudioRecord.READ_BLOCKING)
                if (n <= 0) continue
                var pk = 0f
                for (i in 0 until n) pk = maxOf(pk, kotlin.math.abs(blk[i]))
                level = pk
                val m = minOf(n, buf.size - len)
                System.arraycopy(blk, 0, buf, len, m)
                len += m
                if (len >= buf.size) going = false
            }
            rec.stop()
            rec.release()
        }.apply { start() }
        return null
    }

    fun stop(): Take {
        going = false
        thread?.join()
        val n = len / 2
        return Take(FloatArray(n) { buf[2 * it] }, FloatArray(n) { buf[2 * it + 1] }, RATE)
    }

    fun dir(): File = (ctx.getExternalFilesDir("samples") ?: File(ctx.filesDir, "samples")).apply { mkdirs() }

    /* a take as a WAV file: 32-bit float, stereo */
    fun save(t: Take, f: File) {
        val data = t.n * 8
        val b = ByteBuffer.allocate(44 + data).order(ByteOrder.LITTLE_ENDIAN)
        b.put("RIFF".toByteArray()).putInt(36 + data).put("WAVE".toByteArray())
        b.put("fmt ".toByteArray()).putInt(16).putShort(3).putShort(2).putInt(t.rate).putInt(t.rate * 8)
            .putShort(8).putShort(32)
        b.put("data".toByteArray()).putInt(data)
        for (i in 0 until t.n) b.putFloat(t.l[i]).putFloat(t.r[i])
        RandomAccessFile(f, "rw").use { it.setLength(0); it.write(b.array()) }
    }

    /* one saved by save() back */
    fun load(f: File): Take {
        val b = ByteBuffer.wrap(f.readBytes()).order(ByteOrder.LITTLE_ENDIAN)
        val rate = b.getInt(24)
        val n = (b.capacity() - 44) / 8
        b.position(44)
        val l = FloatArray(n)
        val r = FloatArray(n)
        for (i in 0 until n) { l[i] = b.float; r[i] = b.float }
        return Take(l, r, rate)
    }
}
