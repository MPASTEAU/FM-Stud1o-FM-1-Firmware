package fr.felucca.midihub

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.zip.CRC32
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin

/*
 * A user sample slot as the FM-1 takes it (tools/sampleio.py user_slot, firmware/src/eng_sample.c): mono, 22050 Hz,
 * IMA ADPCM (4 bits, low nibble first), a 480-byte header (magic, zones, name, data length, CRC32), 80 KiB in all
 * (7.38 s). One zone here: the whole keyboard, its root ROOT.
 */
object Slot {
    const val RATE = 22050
    const val DATA_OFF = 512                       // the data's place in the slot (SMP_WRITE offsets)
    const val MAX_DATA = 0x14000 - DATA_OFF        // 81408 B of ADPCM
    const val MAX_SAMPLES = MAX_DATA * 2           // 162816 samples, 7.38 s
    private const val HDR_LEN = 32 + 16 * 28

    private val STEP = intArrayOf(7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55,
        60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
        598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
        4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500,
        20350, 22385, 24623, 27086, 29794, 32767)
    private val IDX = intArrayOf(-1, -1, -1, -1, 2, 4, 6, 8)

    /* the resampler: a Blackman-windowed sinc, cut-off 0.47 x the lower rate (no fold-back), 16 zero crossings a side */
    fun resample(x: FloatArray, from: Int, to: Int): FloatArray {
        if (from == to) return x.copyOf()
        val ratio = to.toDouble() / from
        val fc = 0.47 * min(1.0, ratio)                      // cycles per input sample
        val half = ceil(8.0 / fc).toInt()                    // input samples a side (16 crossings)
        val n = floor(x.size * ratio).toInt()
        val y = FloatArray(n)
        for (i in 0 until n) {
            val t = i / ratio
            val c = floor(t).toInt()
            var acc = 0.0
            for (j in max(0, c - half + 1)..min(x.size - 1, c + half)) {
                val d = j - t
                val w = 0.42 + 0.5 * cos(PI * d / half) + 0.08 * cos(2 * PI * d / half)
                val s = if (abs(d) < 1e-9) 2 * fc else sin(2 * PI * fc * d) / (PI * d)
                acc += x[j] * s * w
            }
            y[i] = acc.toFloat()
        }
        return y
    }

    /* float -> int16, the peak at 30000 (as tools/sampleio.py to_int16) */
    fun toInt16(x: FloatArray): ShortArray {
        var pk = 1e-9f
        for (v in x) pk = max(pk, abs(v))
        return ShortArray(x.size) { (x[it] / pk * 30000f).roundToInt().coerceIn(-32768, 32767).toShort() }
    }

    fun encode(s: ShortArray): ByteArray {
        var pred = 0
        var idx = 0
        val out = ByteArray((s.size + 1) / 2)
        for (n in s.indices) {
            val step = STEP[idx]
            var diff = s[n] - pred
            var code = 0
            if (diff < 0) { code = 8; diff = -diff }
            var vd = step shr 3
            if (diff >= step) { code = code or 4; diff -= step; vd += step }
            if (diff >= step shr 1) { code = code or 2; diff -= step shr 1; vd += step shr 1 }
            if (diff >= step shr 2) { code = code or 1; vd += step shr 2 }
            pred = (if (code and 8 != 0) pred - vd else pred + vd).coerceIn(-32768, 32767)
            idx = (idx + IDX[code and 7]).coerceIn(0, 88)
            out[n / 2] = (out[n / 2].toInt() or (code shl (4 * (n and 1)))).toByte()
        }
        return out
    }

    /* what the FM-1 will play (its decoder: the same steps), for the preview */
    fun decode(d: ByteArray, n: Int): ShortArray {
        var pred = 0
        var idx = 0
        return ShortArray(n) { i ->
            val code = (d[i / 2].toInt() shr (4 * (i and 1))) and 15
            val step = STEP[idx]
            var vd = step shr 3
            if (code and 4 != 0) vd += step
            if (code and 2 != 0) vd += step shr 1
            if (code and 1 != 0) vd += step shr 2
            pred = (if (code and 8 != 0) pred - vd else pred + vd).coerceIn(-32768, 32767)
            idx = (idx + IDX[code and 7]).coerceIn(0, 88)
            pred.toShort()
        }
    }

    /* the 480-byte header (SMP_END): one zone, keys 0..127, root ROOT */
    fun header(name: String, n: Int, data: ByteArray, root: Int): ByteArray {
        val b = ByteBuffer.allocate(HDR_LEN).order(ByteOrder.LITTLE_ENDIAN)
        val crc = CRC32().apply { update(data) }
        val nm = name.uppercase().filter { it in ' '..'~' }.take(8).toByteArray(Charsets.US_ASCII).copyOf(8)
        b.putInt(0x504D5346).putShort(1).put(1).put(0).put(nm).putInt(data.size).putInt(crc.value.toInt())
        b.position(32)
        b.putInt(0).putInt(n).putInt(0).putInt(n - 1).putInt((RATE / 44100.0 * 65536).roundToInt())
        b.putShort((root * 16).toShort()).putShort(0).put(0).put(0).put(127).put(0)
        return b.array()
    }

    /* 7 bytes at a time, their top bits first (the editor protocol's pack7) */
    fun pack7(b: ByteArray): ByteArray {
        val out = ArrayList<Byte>()
        for (i in b.indices step 7) {
            var top = 0
            for (j in 0 until min(7, b.size - i)) top = top or (((b[i + j].toInt() shr 7) and 1) shl j)
            out.add(top.toByte())
            for (j in 0 until min(7, b.size - i)) out.add((b[i + j].toInt() and 0x7F).toByte())
        }
        return out.toByteArray()
    }

    /* the slot to slot k (0: USR1) over the FM-1's MIDI (SMP_BEGIN, SMP_WRITE x n, SMP_END); progress 0..1 */
    fun upload(k: Int, hdr: ByteArray, data: ByteArray, progress: (Float) -> Unit) {
        Hub.ping()
        if (Hub.request(11, byteArrayOf(k.toByte()))[1].toInt() != 0) throw IllegalStateException("SMP_BEGIN refusé")
        for (off in data.indices step 256) {
            val a = DATA_OFF + off
            val args = byteArrayOf(k.toByte(), (a and 0x7F).toByte(), ((a shr 7) and 0x7F).toByte(),
                ((a shr 14) and 0x7F).toByte()) + pack7(data.copyOfRange(off, min(data.size, off + 256)))
            val r = Hub.request(12, args)
            if (r.size < 5 || r[4].toInt() != 0) throw IllegalStateException("écriture à $a refusée (${r.getOrNull(4)})")
            progress(min(data.size, off + 256).toFloat() / data.size)
        }
        val r = Hub.request(13, byteArrayOf(k.toByte()) + pack7(hdr), 5000)
        if (r[1].toInt() != 0) throw IllegalStateException("SMP_END refusé (${r[1]})")
    }
}
