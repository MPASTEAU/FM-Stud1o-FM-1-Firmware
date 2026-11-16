package fr.felucca.midihub

import android.app.AlertDialog
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.view.Gravity
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
 * The sampler. REC / STOP (the phone's microphones or the FM-1, ENTRÉE), and the take is cleaned (Clean), shown and
 * pushed to the FM-1's USR1 at once (as much as the slot holds: 7.38 s), unless it holds no sound.
 * Cutting it by hand: the whole take above (drag a marker roughly), the part picked below, zoomed (drag a marker
 * finely; a tap elsewhere plays from there). ▶ plays the part picked on the phone as recorded, ▶ FM-1 as the FM-1 will
 * play it (mono, 22 kHz, ADPCM), ▶ SUR FM-1 on the FM-1 itself (its selected track, made SAMPLE / USR1 when asked).
 * ENVOYER pushes the part picked again; AUTO puts the markers back round the sound. PRISES: the takes kept (raw and
 * cleaned WAV, 48 kHz 32-bit float, stereo).
 */
class SamplerView(ctx: Context) : LinearLayout(ctx) {
    private val rec = Recorder(ctx)
    private val all = WaveView(ctx, false)
    private val zoom = WaveView(ctx, true)
    private val status = TextView(ctx).apply { textSize = 14f; setPadding(32, 12, 32, 12) }
    private val bRec = Button(ctx)
    private val bIn = Button(ctx)
    private val bRev = Button(ctx)
    private val bNote = TextView(ctx).apply { textSize = 16f; gravity = Gravity.CENTER }
    private var take: Take? = null
    private var name = "REC"
    private var root = 60
    private var reverse = false
    private var report = ""
    private var sent = ""                              // what USR1 holds (the part's key), "" none from here
    @Volatile private var busy = false
    private var player: AudioTrack? = null
    private var playFrom = 0                           // the take's index the playback started at
    private var playStep = 1.0                         // take samples per played frame (the FM-1 preview: 48 / 22.05)

    init {
        orientation = VERTICAL
        fun btn(t: String, f: () -> Unit) = Button(ctx).apply { text = t; isAllCaps = false; setOnClickListener { f() } }
        fun row(vararg v: View) = LinearLayout(ctx).apply { v.forEach { addView(it, LayoutParams(0, WRAP_CONTENT, 1f)) } }
        addView(row(bRec, bIn, btn("PRISES") { takes() }))
        addView(all, LayoutParams(MATCH_PARENT, 0, 1f))
        addView(zoom, LayoutParams(MATCH_PARENT, 0, 2f))
        addView(row(btn("▶") { play(false) }, btn("▶ FM-1") { play(true) }, btn("■") { stopPlay() }, btn("AUTO") { auto() }))
        addView(row(bRev, btn("−") { setRoot(root - 1) }, bNote, btn("+") { setRoot(root + 1) }))
        addView(row(btn("ENVOYER") { send() }, btn("▶ SUR FM-1") { onFm1() }))
        addView(status, LayoutParams(MATCH_PARENT, WRAP_CONTENT))
        bRec.setOnClickListener { if (rec.recording) stopRec() else startRec() }
        bIn.setOnClickListener { if (!rec.recording) { rec.fromFm1 = !rec.fromFm1; labels() } }
        bRev.setOnClickListener { reverse = !reverse; labels(); changed() }
        all.onMove = { a, b -> zoom.setSel(a, b, false); changed() }
        zoom.onMove = { a, b -> all.setSel(a, b, true); changed() }
        all.onTap = { i -> play(false, i) }
        zoom.onTap = { i -> play(false, i) }
        setRoot(60)
        say("● REC : les micros du téléphone (ENTRÉE). À l'arrêt : nettoyé, normalisé, envoyé dans USR1.")
    }

    private fun say(s: String) = post { status.text = if (report.isEmpty()) s else "$report\n$s" }

    private fun labels() {
        bRec.text = if (rec.recording) "■ STOP" else "● REC"
        bIn.text = if (rec.fromFm1) "ENTRÉE FM-1" else "ENTRÉE MICROS"
        bRev.text = if (reverse) "REV ON" else "REV"
        val names = arrayOf("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")
        bNote.text = "NOTE ${names[root % 12]}${root / 12 - 1}"
    }

    private fun setRoot(n: Int) {
        root = n.coerceIn(24, 96)
        labels()
        changed()
    }

    /* the part picked: what it is (sent or not) */
    private fun key() = "${all.a} ${all.b} $reverse $root ${System.identityHashCode(take)}"
    private fun changed() {
        if (take != null && sent.isNotEmpty() && sent != key()) say("modifié : ENVOYER pour mettre à jour USR1")
    }

    private val meter = object : Runnable {
        override fun run() {
            if (!rec.recording) return
            val bars = (rec.level.coerceIn(0f, 1f) * 20).toInt()
            status.text = "● %.1f s  %s\n%s%s".format(rec.seconds, rec.device, "█".repeat(bars), if (rec.level >= 0.99f) "  SATURE" else "")
            postDelayed(this, 100)
        }
    }

    private fun startRec() {
        if (busy) return
        stopPlay()
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
                name = "R" + SimpleDateFormat("ddHHmmss", Locale.ROOT).format(Date()).takeLast(7)
                rec.save(raw, File(rec.dir(), "$name-brut.wav"))
                val (clean, rep) = Clean.run(raw)
                if (rep.sound) rec.save(clean, File(rec.dir(), "$name.wav"))
                report = if (!rep.sound) "Aucun son détecté (crête %.0f dBFS) : rien envoyé. Vérifie l'ENTRÉE.".format(rep.peakDb)
                else "%s · %.1f s · bruit %.0f dBFS%s · silences −%.1f s".format(if (raw.stereo()) "stéréo" else "mono",
                    clean.n / clean.rate.toFloat(), rep.noiseDb, if (rep.cutDb > 0) " réduit" else "", rep.trimmed)
                busy = false
                post {
                    show(clean)
                    if (rep.sound) send() else say("")
                }
            } catch (e: Exception) {
                busy = false
                say("Erreur : ${e.message}")
            }
        }.start()
    }

    private fun show(t: Take) {
        take = t
        sent = ""
        all.set(t, limit(t))
        zoom.set(t, limit(t))
        all.setSel(0, min(t.n, limit(t)), true)
        zoom.setSel(0, min(t.n, limit(t)), false)
    }

    private fun limit(t: Take) = (Slot.MAX_SAMPLES.toLong() * t.rate / Slot.RATE).toInt()

    /* the markers back round the sound of the take, at most the slot's length */
    private fun auto() {
        val t = take ?: return
        val m = t.mono()
        var pk = 1e-9f
        for (v in m) pk = max(pk, abs(v))
        val a = max(0, m.indexOfFirst { abs(it) > pk * 0.01f } - t.rate / 100)
        val b = min(min(t.n, a + limit(t)), m.indexOfLast { abs(it) > pk * 0.01f } + t.rate * 6 / 100)
        all.setSel(a, max(a + 1, b), true)
        zoom.setSel(a, max(a + 1, b), false)
        changed()
    }

    /* the part picked as the FM-1 gets it: 22050 Hz mono int16 (faded, reversed) */
    private fun build(t: Take): ShortArray {
        val a = all.a
        val b = min(all.b, a + limit(t))
        val part = t.slice(a, b)
        if (reverse) { part.l.reverse(); part.r.reverse() }
        Clean.fade(part, part.rate * 3 / 1000, part.rate / 50)
        var x = Slot.resample(part.mono(), part.rate, Slot.RATE)
        if (x.size > Slot.MAX_SAMPLES) x = x.copyOf(Slot.MAX_SAMPLES)
        return Slot.toInt16(x)
    }

    private fun send() {
        val t = take ?: return
        if (busy) return
        if (!Hub.fm1Here) { say("FM-1 non branché : ENVOYER une fois branché"); return }
        busy = true
        val k = key()
        Thread {
            try {
                val s = build(t)
                val data = Slot.encode(s)
                Slot.upload(0, Slot.header(name, s.size, data, root), data) { p -> say("Envoi dans USR1… %d %%".format((p * 100).toInt())) }
                sent = k
                say("USR1 ← %s, %.2f s ✓".format(name, s.size / Slot.RATE.toFloat()))
            } catch (e: Exception) {
                say("Envoi raté : ${e.message} (transport à l'arrêt ?)")
            }
            busy = false
        }.start()
    }

    /* ---- playback on the phone (its speaker or headphones), the playhead on both views */
    private fun play(fm1: Boolean, from: Int = all.a) {
        val t = take ?: return
        stopPlay()
        val track: AudioTrack
        if (fm1) {
            val s = build(t)
            val d = Slot.decode(Slot.encode(s), s.size)
            track = audioTrack(Slot.RATE, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT, d.size * 2)
            track.write(d, 0, d.size)
            playFrom = all.a
            playStep = t.rate.toDouble() / Slot.RATE
        } else {
            val a = from.coerceIn(all.a, all.b - 1)
            val n = all.b - a
            val f = FloatArray(n * 2)
            for (i in 0 until n) {
                val j = if (reverse) all.b - 1 - (a - all.a) - i else a + i
                f[2 * i] = t.l[j]; f[2 * i + 1] = t.r[j]
            }
            track = audioTrack(t.rate, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_FLOAT, f.size * 4)
            track.write(f, 0, f.size, AudioTrack.WRITE_BLOCKING)
            playFrom = a
            playStep = 1.0
        }
        player = track
        track.play()
        post(head)
    }

    private fun audioTrack(rate: Int, ch: Int, enc: Int, bytes: Int) = AudioTrack.Builder()
        .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
            .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
        .setAudioFormat(AudioFormat.Builder().setSampleRate(rate).setChannelMask(ch).setEncoding(enc).build())
        .setTransferMode(AudioTrack.MODE_STATIC).setBufferSizeInBytes(bytes).build()

    private val head = object : Runnable {
        override fun run() {
            val p = player ?: return
            val frames = p.playbackHeadPosition
            if (frames >= p.bufferSizeInFrames || p.playState != AudioTrack.PLAYSTATE_PLAYING) { stopPlay(); return }
            val off = (frames * playStep).toInt()
            val i = if (!reverse) playFrom + off else if (playStep == 1.0) all.b - 1 - (playFrom - all.a) - off else all.b - off
            all.head = i
            zoom.head = i
            all.invalidate(); zoom.invalidate()
            postDelayed(this, 30)
        }
    }

    private fun stopPlay() {
        removeCallbacks(head)
        player?.let { runCatching { it.stop() }; it.release() }
        player = null
        all.head = -1; zoom.head = -1
        all.invalidate(); zoom.invalidate()
    }

    /* ---- on the FM-1 itself: its selected track, SAMPLE / USR1 (made so when asked) */
    private fun onFm1() {
        if (!Hub.fm1Here) { say("FM-1 non branché"); return }
        if (sent.isEmpty()) { say("Rien d'envoyé dans USR1 depuis ici : ENVOYER d'abord"); return }
        Thread {
            try {
                val k = Fm1.selected()
                if (Fm1.isSampler(k)) { note(); return@Thread }
                post {
                    AlertDialog.Builder(context).setTitle("Piste T${k + 1}")
                        .setMessage("La piste sélectionnée sur le FM-1 (T${k + 1}) ne joue pas USR1. La passer en SAMPLE / USR1 ? Son son est remplacé, ses notes restent.")
                        .setPositiveButton("OUI") { _, _ ->
                            Thread {
                                try { Fm1.makeSampler(k); say("T${k + 1} : SAMPLE / USR1"); note() } catch (e: Exception) { say("Raté : ${e.message}") }
                            }.start()
                        }
                        .setNegativeButton("NON", null).show()
                }
            } catch (e: Exception) {
                say("Le FM-1 ne répond pas : ${e.message}")
            }
        }.start()
    }

    private fun note() {
        val t = take ?: return
        Hub.note(root, 110, (min(all.b - all.a, limit(t)) * 1000L / t.rate) + 50)
    }

    /* ---- the takes kept */
    private fun takes() {
        if (rec.recording || busy) return
        val files = rec.dir().listFiles { f -> f.name.endsWith(".wav") && !f.name.endsWith("-brut.wav") }
            ?.sortedByDescending { it.lastModified() } ?: emptyList()
        if (files.isEmpty()) { say("Aucune prise gardée"); return }
        AlertDialog.Builder(context).setTitle("Prises")
            .setItems(files.map { f -> "%s  %.1f s".format(f.nameWithoutExtension, (f.length() - 44) / 8f / Recorder.RATE) }.toTypedArray()) { _, i ->
                busy = true
                Thread {
                    try {
                        val t = rec.load(files[i])
                        name = files[i].nameWithoutExtension.take(8)
                        report = "Prise $name"
                        post { show(t); say("ENVOYER pour la mettre dans USR1") }
                    } catch (e: Exception) { say("Lecture ratée : ${e.message}") }
                    busy = false
                }.start()
            }.show()
    }
}

/* a take's waveform: the whole take (zoomed false) or the part picked with a margin (true). Two markers: a touch near
 * one drags it (the nearer within reach), a tap elsewhere plays from there; what is past the slot's 7.38 s shaded red */
class WaveView(ctx: Context, private val zoomed: Boolean) : View(ctx) {
    var a = 0
    var b = 0
    var head = -1
    var onMove: ((Int, Int) -> Unit)? = null
    var onTap: ((Int) -> Unit)? = null
    private var take: Take? = null
    private var mono = FloatArray(0)
    private var limit = 0
    private var v0 = 0                                 // the view's first and last sample
    private var v1 = 1
    private var drag = 0                               // 1 a, 2 b, 0 none
    private var downX = 0f
    private val pw = Paint().apply { color = Color.rgb(90, 200, 255) }
    private val pm = Paint().apply { color = Color.WHITE; strokeWidth = 5f }
    private val pd = Paint().apply { color = Color.argb(160, 0, 0, 0) }
    private val pr = Paint().apply { color = Color.argb(90, 255, 60, 60) }
    private val ph = Paint().apply { color = Color.rgb(255, 200, 0); strokeWidth = 3f }
    private val pt = Paint().apply { color = Color.WHITE; textSize = 32f }

    fun set(t: Take, lim: Int) {
        take = t
        mono = t.mono()
        limit = lim
        v0 = 0; v1 = t.n
        invalidate()
    }

    /* the markers (and, zoomed, the view round them, unless keepView) */
    fun setSel(na: Int, nb: Int, keepView: Boolean) {
        a = na; b = nb
        if (zoomed && !keepView && drag == 0) fit()
        invalidate()
    }

    private fun fit() {
        val t = take ?: return
        val pad = max((b - a) / 8, t.rate / 20)
        v0 = max(0, a - pad); v1 = min(t.n, b + pad)
    }

    private fun x(i: Int) = (i - v0).toFloat() / max(1, v1 - v0) * width
    private fun idx(x: Float) = (v0 + (x / width).coerceIn(0f, 1f) * (v1 - v0)).toInt()

    override fun onDraw(c: Canvas) {
        c.drawColor(if (zoomed) Color.rgb(26, 26, 32) else Color.rgb(18, 18, 22))
        val t = take ?: run { if (!zoomed) c.drawText("Pas de prise", 32f, height / 2f, pt); return }
        val mid = height / 2f
        val span = (v1 - v0).toDouble() / width
        for (px in 0 until width) {
            val s = (v0 + px * span).toInt()
            val e = min(mono.size, max(s + 1, (v0 + (px + 1) * span).toInt()))
            var lo = 0f
            var hi = 0f
            for (i in s until e) { lo = min(lo, mono[i]); hi = max(hi, mono[i]) }
            c.drawRect(px.toFloat(), mid - hi * mid, px + 1f, mid - lo * mid + 1, pw)
        }
        c.drawRect(0f, 0f, x(a), height.toFloat(), pd)
        c.drawRect(x(b), 0f, width.toFloat(), height.toFloat(), pd)
        if (b - a > limit) c.drawRect(x(a + limit), 0f, x(b), height.toFloat(), pr)
        c.drawLine(x(a), 0f, x(a), height.toFloat(), pm)
        c.drawLine(x(b), 0f, x(b), height.toFloat(), pm)
        if (head >= 0) c.drawLine(x(head), 0f, x(head), height.toFloat(), ph)
        if (!zoomed) {
            val sec = min(b - a, limit) / t.rate.toFloat()
            c.drawText("%.2f s / %.1f s%s".format(sec, t.n / t.rate.toFloat(), if (b - a > limit) "   (rouge : au-delà de 7,38 s, coupé)" else ""), 20f, 38f, pt)
        } else c.drawText("ZOOM", 20f, 38f, pt)
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        if (take == null) return false
        val reach = 70f
        when (e.action) {
            MotionEvent.ACTION_DOWN -> {
                downX = e.x
                val da = abs(e.x - x(a))
                val db = abs(e.x - x(b))
                drag = if (min(da, db) > reach) 0 else if (da < db) 1 else 2
                parent.requestDisallowInterceptTouchEvent(true)
            }
            MotionEvent.ACTION_MOVE -> if (drag != 0) {
                val i = idx(e.x)
                if (drag == 1) a = min(i, b - 1) else b = max(i, a + 1)
                onMove?.invoke(a, b)
                invalidate()
            }
            MotionEvent.ACTION_UP -> {
                if (drag == 0 && abs(e.x - downX) < 20) onTap?.invoke(idx(e.x))
                else if (drag != 0 && zoomed) { drag = 0; fit(); invalidate() }
                drag = 0
            }
        }
        return true
    }
}
