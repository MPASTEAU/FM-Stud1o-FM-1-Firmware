package fr.felucca.midihub

import android.annotation.SuppressLint
import android.content.Context
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioRecord
import android.media.MediaRecorder
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder

/*
 * The phone's microphones at their best: 48 kHz, 32-bit float, stereo (both microphones), the raw source when the
 * phone has one (UNPROCESSED: no gain control, no noise suppression; else CAMCORDER, the stereo video source, else MIC).
 * A take is kept as it came (WAV, 32-bit float) next to its cleaned copy (getExternalFilesDir("samples")).
 */
class Recorder(private val ctx: Context) {
    companion object {
        const val RATE = 48000
        const val MAX_S = 60                                  // a take's longest, seconds
        val SOURCES = listOf(MediaRecorder.AudioSource.UNPROCESSED to "BRUT", MediaRecorder.AudioSource.CAMCORDER to "VIDÉO",
            MediaRecorder.AudioSource.MIC to "MICRO")
    }

    var source = if (unprocessed()) 0 else 1
    @Volatile private var going = false
    private var thread: Thread? = null
    private var buf = FloatArray(RATE * 2 * MAX_S)
    @Volatile private var len = 0                            // floats (interleaved L R)
    @Volatile var level = 0f                                 // the last block's peak (the meter)

    private fun unprocessed() =
        ctx.getSystemService(AudioManager::class.java).getProperty(AudioManager.PROPERTY_SUPPORT_AUDIO_SOURCE_UNPROCESSED) == "true"

    val recording get() = going
    val seconds get() = len / 2f / RATE

    @SuppressLint("MissingPermission")
    fun start(): String? {
        val fmt = AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_FLOAT).setSampleRate(RATE)
            .setChannelMask(AudioFormat.CHANNEL_IN_STEREO).build()
        val min = AudioRecord.getMinBufferSize(RATE, AudioFormat.CHANNEL_IN_STEREO, AudioFormat.ENCODING_PCM_FLOAT)
        val rec = try {
            AudioRecord.Builder().setAudioSource(SOURCES[source].first).setAudioFormat(fmt)
                .setBufferSizeInBytes(maxOf(min, RATE * 8 / 5)).build()
        } catch (e: Exception) {
            return "micro indisponible : ${e.message}"
        }
        if (rec.state != AudioRecord.STATE_INITIALIZED) {
            rec.release()
            return "micro indisponible (source ${SOURCES[source].second})"
        }
        len = 0
        going = true
        rec.startRecording()
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
}
