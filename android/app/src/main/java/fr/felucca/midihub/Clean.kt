package fr.felucca.midihub

import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin
import kotlin.math.sqrt
import kotlin.math.tan

/*
 * A take cleaned before it goes to the FM-1 (the phone's full-quality take, stereo): the rumble and DC away (a 40 Hz
 * high-pass), the steady noise (hiss, hum, room) taken out by a spectral gate learned from the take's quietest
 * moments, the silences at either end cut (short fades), the peak to -0.5 dBFS.
 */
class Take(val l: FloatArray, val r: FloatArray, val rate: Int) {
    val n get() = l.size
    fun copy() = Take(l.copyOf(), r.copyOf(), rate)
    fun mono() = FloatArray(n) { (l[it] + r[it]) * 0.5f }
    fun slice(a: Int, b: Int) = Take(l.copyOfRange(a, b), r.copyOfRange(a, b), rate)
    /* the two microphones heard apart (not one copied to both channels) */
    fun stereo(): Boolean {
        for (i in 0 until n) if (abs(l[i] - r[i]) > 1e-5f) return true
        return false
    }
}

object Clean {
    class Report(val noiseDb: Float, val cutDb: Float, val trimmed: Float)

    fun run(t: Take): Pair<Take, Report> {
        val c = t.copy()
        highpass(c.l, c.rate, 40.0)
        highpass(c.r, c.rate, 40.0)
        val (noise, cut) = denoise(c)
        val before = c.n
        val out = trim(c)
        normalize(out, 0.944f)                       // -0.5 dBFS
        return out to Report(noise, cut, (before - out.n).toFloat() / c.rate)
    }

    /* a 2nd-order Butterworth high-pass (bilinear) */
    private fun highpass(x: FloatArray, rate: Int, fc: Double) {
        val k = tan(PI * fc / rate)
        val q = sqrt(2.0)
        val norm = 1 / (1 + k * q + k * k)
        val b0 = norm
        val b1 = -2 * norm
        val a1 = 2 * (k * k - 1) * norm
        val a2 = (1 - k * q + k * k) * norm
        var x1 = 0.0
        var x2 = 0.0
        var y1 = 0.0
        var y2 = 0.0
        for (i in x.indices) {
            val v = x[i].toDouble()
            val y = b0 * v + b1 * x1 + b0 * x2 - a1 * y1 - a2 * y2
            x2 = x1; x1 = v; y2 = y1; y1 = y
            x[i] = y.toFloat()
        }
    }

    private const val N = 2048
    private const val HOP = N / 4

    /* the spectral gate: the noise's spectrum from the quietest tenth of the frames (both channels), each bin then
     * kept as far as it stands above it (1 - 1.5 noise / level, at least -24 dB), smoothed over time (no warble).
     * Nothing done when the take has no quiet moment to learn from (the noise within 12 dB of the loud). Returns
     * the noise floor (dBFS) and how far it went down (dB) */
    private fun denoise(t: Take): Pair<Float, Float> {
        val frames = (t.n - N) / HOP + 1
        if (frames < 8) return 0f to 0f
        val win = DoubleArray(N) { 0.5 - 0.5 * cos(2 * PI * it / N) }
        val mono = t.mono()
        val energy = DoubleArray(frames) { f ->
            var e = 0.0
            for (i in 0 until N) { val v = mono[f * HOP + i] * win[i]; e += v * v }
            e
        }
        val order = (0 until frames).sortedBy { energy[it] }
        val quiet = order.take(max(2, frames / 10))
        val loud = energy[order[(frames * 9) / 10]]
        val qe = quiet.sumOf { energy[it] } / quiet.size
        val noiseDb = (10 * log10(qe / (win.sumOf { it * it }) + 1e-20)).toFloat()
        if (qe * 16 > loud) return noiseDb to 0f                // no clear noise floor: leave it
        val prof = DoubleArray(N / 2 + 1)
        val re = DoubleArray(N)
        val im = DoubleArray(N)
        for (ch in listOf(t.l, t.r)) for (f in quiet) {
            for (i in 0 until N) { re[i] = ch[f * HOP + i] * win[i]; im[i] = 0.0 }
            fft(re, im)
            for (k in prof.indices) prof[k] += hypot(re[k], im[k]) / (2 * quiet.size)
        }
        val floor = 0.063                                       // -24 dB
        for (ch in listOf(t.l, t.r)) {
            val out = DoubleArray(ch.size)
            val prev = DoubleArray(prof.size) { 1.0 }
            for (f in 0 until frames) {
                for (i in 0 until N) { re[i] = ch[f * HOP + i] * win[i]; im[i] = 0.0 }
                fft(re, im)
                for (k in prof.indices) {
                    val m = hypot(re[k], im[k])
                    var g = max(floor, 1 - 1.5 * prof[k] / (m + 1e-12))
                    g = if (g > prev[k]) g else 0.5 * (g + prev[k])  // opens at once, closes over a few frames
                    prev[k] = g
                    re[k] *= g; im[k] *= g
                    if (k in 1 until N / 2) { re[N - k] = re[k]; im[N - k] = -im[k] }
                }
                for (i in 0 until N) im[i] = -im[i]             // the inverse: conj, fft, conj, / N
                fft(re, im)
                for (i in 0 until N) out[f * HOP + i] += re[i] / N * win[i] / 1.5   // (Hann^2 at 75 %: 1.5)
            }
            val done = (frames - 1) * HOP + N
            for (i in 0 until min(done, ch.size)) ch[i] = out[i].toFloat()
            for (i in done until ch.size) ch[i] = 0f
        }
        return noiseDb to (-20 * log10(floor)).toFloat()
    }

    /* the silence at either end cut: the first and last moment above -40 dB of the peak, 10 ms before, 60 ms after;
     * 3 ms in, 20 ms out */
    private fun trim(t: Take): Take {
        val m = t.mono()
        var pk = 1e-9f
        for (v in m) pk = max(pk, abs(v))
        val thr = pk * 0.01f
        var a = m.indexOfFirst { abs(it) > thr }
        var b = m.indexOfLast { abs(it) > thr }
        if (a < 0) return t
        a = max(0, a - t.rate / 100)
        b = min(t.n, b + t.rate * 6 / 100)
        val s = t.slice(a, b)
        fade(s, t.rate * 3 / 1000, t.rate / 50)
        return s
    }

    fun fade(t: Take, fin: Int, fout: Int) {
        for (i in 0 until min(fin, t.n)) {
            val g = i.toFloat() / fin
            t.l[i] *= g; t.r[i] *= g
        }
        for (i in 0 until min(fout, t.n)) {
            val g = (0.5 + 0.5 * cos(PI * (i + 1) / fout)).toFloat()
            val j = t.n - fout + i
            if (j >= 0) { t.l[j] *= g; t.r[j] *= g }
        }
    }

    fun normalize(t: Take, to: Float) {
        var pk = 1e-9f
        for (i in 0 until t.n) pk = max(pk, max(abs(t.l[i]), abs(t.r[i])))
        val g = to / pk
        for (i in 0 until t.n) { t.l[i] *= g; t.r[i] *= g }
    }

    /* in place, radix 2 */
    private fun fft(re: DoubleArray, im: DoubleArray) {
        val n = re.size
        var j = 0
        for (i in 1 until n) {
            var bit = n shr 1
            while (j and bit != 0) { j = j xor bit; bit = bit shr 1 }
            j = j xor bit
            if (i < j) {
                var t = re[i]; re[i] = re[j]; re[j] = t
                t = im[i]; im[i] = im[j]; im[j] = t
            }
        }
        var len = 2
        while (len <= n) {
            val ang = -2 * PI / len
            val wr = cos(ang)
            val wi = sin(ang)
            for (i in 0 until n step len) {
                var cr = 1.0
                var ci = 0.0
                for (k in 0 until len / 2) {
                    val ar = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci
                    val ai = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr
                    re[i + k + len / 2] = re[i + k] - ar
                    im[i + k + len / 2] = im[i + k] - ai
                    re[i + k] += ar
                    im[i + k] += ai
                    val t = cr * wr - ci * wi
                    ci = cr * wi + ci * wr
                    cr = t
                }
            }
            len = len shl 1
        }
    }
}
