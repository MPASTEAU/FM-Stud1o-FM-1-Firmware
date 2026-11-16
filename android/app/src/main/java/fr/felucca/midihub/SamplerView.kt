package fr.felucca.midihub

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

/*
 * The sampler: REC, STOP, and the take is cleaned (Clean), shown, and pushed to the FM-1's USR1 at once (as much of
 * it as the slot holds: 7.38 s). The two markers pick another part of it, REVERSE turns it round, NOTE its root on
 * the keys; ENVOYER pushes again. ÉCOUTER plays it on the FM-1 itself (a note to the selected track: SAMPLE, SET
 * USR1), out of its own outputs. Every take is kept, raw and cleaned (WAV, 48 kHz 32-bit float, stereo).
 */
class SamplerView(ctx: Context) : LinearLayout(ctx) {
    private val rec = Recorder(ctx)
    private val wave = WaveView(ctx)
    private val status = TextView(ctx).apply { textSize = 14f; setPadding(32, 16, 32, 16) }
    private val bRec = Button(ctx)
    private val bSrc = Button(ctx)
    private val bPlay = Button(ctx).apply { text = "ÉCOUTER" }
    private val bSend = Button(ctx).apply { text = "ENVOYER" }
    private val bRev = Button(ctx)
    private val bNote = TextView(ctx).apply { textSize = 16f; setPadding(16, 0, 16, 0) }
    private var take: Take? = null                     // the cleaned take
    private var name = "REC"
    private var root = 60
    private var reverse = false
    @Volatile private var busy = false
    private var sentLen = 0L                           // the sample pushed, ms (ÉCOUTER's note)
    private var report = ""                            // the take's cleaning, above what the sends say

    init {
        orientation = VERTICAL
        fun row(vararg v: View) = LinearLayout(ctx).apply { v.forEach { addView(it, LayoutParams(0, WRAP_CONTENT, 1f)) } }
        val minus = Button(ctx).apply { text = "−"; setOnClickListener { setRoot(root - 1) } }
        val plus = Button(ctx).apply { text = "+"; setOnClickListener { setRoot(root + 1) } }
        addView(row(bRec, bSrc))
        addView(wave, LayoutParams(MATCH_PARENT, 0, 1f))
        addView(row(bPlay, bSend, bRev))
        addView(row(minus, bNote, plus))
        addView(status, LayoutParams(MATCH_PARENT, WRAP_CONTENT))
        bRec.setOnClickListener { if (rec.recording) stopRec() else startRec() }
        bSrc.setOnClickListener {
            if (!rec.recording) { rec.source = (rec.source + 1) % Recorder.SOURCES.size; labels() }
        }
        bSend.setOnClickListener { send() }
        bPlay.setOnClickListener {
            if (sentLen > 0) Hub.note(root, 110, sentLen) else say("Rien d'envoyé encore")
        }
        bRev.setOnClickListener { reverse = !reverse; labels(); send() }
        wave.onPick = { labels() }
        setRoot(60)
        labels()
        say("REC pour enregistrer (les deux micros, 48 kHz). À l'arrêt : nettoyé, normalisé, envoyé dans USR1.\n" +
            "Pour l'entendre sur le FM-1 : une piste en SAMPLE, SET USR1, sélectionnée.")
    }

    private fun say(s: String) = post { status.text = if (report.isEmpty()) s else "$report\n$s" }

    private fun labels() {
        bRec.text = if (rec.recording) "■ STOP" else "● REC"
        bSrc.text = "SOURCE ${Recorder.SOURCES[rec.source].second}"
        bRev.text = if (reverse) "REVERSE ON" else "REVERSE"
        val names = arrayOf("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")
        bNote.text = "NOTE ${names[root % 12]}${root / 12 - 1}"
        bNote.gravity = android.view.Gravity.CENTER
    }

    private fun setRoot(n: Int) {
        root = n.coerceIn(24, 96)
        labels()
    }

    private val meter = object : Runnable {
        override fun run() {
            if (!rec.recording) return
            val bars = (rec.level.coerceIn(0f, 1f) * 20).toInt()
            status.text = "● %.1f s   %s%s".format(rec.seconds, "█".repeat(bars), if (rec.level >= 0.99f) "  SATURE" else "")
            postDelayed(this, 100)
        }
    }

    private fun startRec() {
        if (busy) return
        report = ""
        val err = rec.start()
        if (err != null) { say(err); return }
        labels()
        post(meter)
    }

    private fun stopRec() {
        val raw = rec.stop()
        labels()
        busy = true
        say("Nettoyage…")
        Thread {
            try {
                val stamp = SimpleDateFormat("HHmmss", Locale.ROOT).format(Date())
                name = "R$stamp"
                rec.save(raw, File(rec.dir(), "$name-brut.wav"))
                val (clean, rep) = Clean.run(raw)
                rec.save(clean, File(rec.dir(), "$name.wav"))
                take = clean
                val limit = (Slot.MAX_SAMPLES.toLong() * clean.rate / Slot.RATE).toInt()
                post {
                    wave.set(clean, limit)
                    labels()
                }
                report = "%s · %.1f s · bruit %.0f dBFS%s · silences −%.1f s".format(
                    if (raw.stereo()) "stéréo" else "mono", clean.n / clean.rate.toFloat(), rep.noiseDb,
                    if (rep.cutDb > 0) " réduit" else " (rien à réduire)", rep.trimmed)
                say("")
                busy = false
                send()
            } catch (e: Exception) {
                say("Erreur : ${e.message}")
                busy = false
            }
        }.start()
    }

    /* the part picked, to USR1 */
    private fun send() {
        val t = take ?: return
        if (busy) return
        if (!Hub.fm1Here) { say("FM-1 non branché : ENVOYER une fois branché"); return }
        busy = true
        val a = wave.a
        val b = min(wave.b, a + (Slot.MAX_SAMPLES.toLong() * t.rate / Slot.RATE).toInt())   /* (the slot's 7.38 s) */
        Thread {
            try {
                val part = t.slice(a, b)
                if (reverse) { part.l.reverse(); part.r.reverse() }
                Clean.fade(part, part.rate * 3 / 1000, part.rate / 50)
                var x = Slot.resample(part.mono(), part.rate, Slot.RATE)
                if (x.size > Slot.MAX_SAMPLES) x = x.copyOf(Slot.MAX_SAMPLES)
                val s = Slot.toInt16(x)
                val data = Slot.encode(s)
                val hdr = Slot.header(name, s.size, data, root)
                Slot.upload(0, hdr, data) { p -> say("Envoi dans USR1… %d %%".format((p * 100).toInt())) }
                sentLen = s.size * 1000L / Slot.RATE
                say("USR1 ← %s, %.1f s ✓  (ÉCOUTER : sur la piste sélectionnée, en SAMPLE / USR1)".format(name, s.size / Slot.RATE.toFloat()))
            } catch (e: Exception) {
                say("Envoi raté : ${e.message} (transport à l'arrêt ? FM-1 branché ?)")
            }
            busy = false
        }.start()
    }
}

/* the take's waveform; two markers (drag the nearer), what is past the slot's 7.38 s from the first shaded */
class WaveView(ctx: Context) : View(ctx) {
    var a = 0
    var b = 0
    var onPick: (() -> Unit)? = null
    private var take: Take? = null
    private var limit = 0
    private var peaks = FloatArray(0)
    private val pw = Paint().apply { color = Color.rgb(90, 200, 255); strokeWidth = 2f }
    private val pm = Paint().apply { color = Color.WHITE; strokeWidth = 4f }
    private val pd = Paint().apply { color = Color.argb(150, 0, 0, 0) }
    private val pr = Paint().apply { color = Color.argb(90, 255, 60, 60) }
    private val pt = Paint().apply { color = Color.WHITE; textSize = 34f }

    fun set(t: Take, lim: Int) {
        take = t
        limit = lim
        a = 0
        b = min(t.n, lim)
        peaks = FloatArray(0)
        invalidate()
    }

    private fun x(i: Int) = i.toFloat() / max(1, take?.n ?: 1) * width
    private fun idx(x: Float) = ((x / width).coerceIn(0f, 1f) * (take?.n ?: 0)).toInt()

    override fun onDraw(c: Canvas) {
        c.drawColor(Color.rgb(20, 20, 24))
        val t = take ?: run { c.drawText("Pas de prise", 32f, height / 2f, pt); return }
        if (peaks.size != width) {
            peaks = FloatArray(width)
            val m = t.mono()
            for (i in m.indices) { val p = i * width / m.size; peaks[p] = max(peaks[p], abs(m[i])) }
        }
        val mid = height / 2f
        for (i in peaks.indices) c.drawLine(i.toFloat(), mid - peaks[i] * mid, i.toFloat(), mid + peaks[i] * mid, pw)
        c.drawRect(0f, 0f, x(a), height.toFloat(), pd)
        c.drawRect(x(b), 0f, width.toFloat(), height.toFloat(), pd)
        if (b - a > limit) c.drawRect(x(a + limit), 0f, x(b), height.toFloat(), pr)
        c.drawLine(x(a), 0f, x(a), height.toFloat(), pm)
        c.drawLine(x(b), 0f, x(b), height.toFloat(), pm)
        val sec = min(b - a, limit) / t.rate.toFloat()
        c.drawText("%.2f s%s".format(sec, if (b - a > limit) "  (7,38 s max : le rouge est coupé)" else ""), 24f, 44f, pt)
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        if (take == null) return false
        val i = idx(e.x)
        if (abs(i - a) < abs(i - b)) a = min(i, b - 1) else b = max(i, a + 1)
        invalidate()
        if (e.action == MotionEvent.ACTION_UP) onPick?.invoke()
        return true
    }
}
