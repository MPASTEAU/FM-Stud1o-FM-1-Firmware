package fr.felucca.midihub

import android.media.midi.MidiDevice
import android.media.midi.MidiDeviceInfo
import android.media.midi.MidiInputPort
import android.media.midi.MidiManager
import android.media.midi.MidiReceiver
import android.os.Handler
import android.os.SystemClock
import java.io.ByteArrayOutputStream

/*
 * The MIDI hub for the FM-1 (tools/midi_hub.py on a phone): every USB MIDI controller on the phone's hub plays the
 * FM-1 ("Felucca"). Every other device's messages go to the FM-1 as they are (clock and active sensing dropped); an
 * Akai MIDI Mix (factory mapping) is translated, strip n -> track n on MIDI channel 8 + n (with MIDI IN SEL, the
 * FM-1's default, channels 9..16 are tracks 1..8's mix):
 *
 *   fader        -> CC7  LEVEL
 *   knob row 1   -> nothing yet
 *   knob row 2   -> CC103 LOW PASS (left: off, right: closed)
 *   knob row 3   -> CC104 HIGH PASS (left: off, right: up to ~1.5 kHz)
 *   MUTE button  -> CC92 the track's MUTE on / off (its notes stop, its tails ring out; its LED lit while muted)
 *   REC ARM      -> selects the track (CC102); its LED lit, the others dark, the FM-1's own choice too (CC102 on
 *                   9 + track from it). A track's notes (CC105 on 9 + track) flash its REC ARM, as LIVE's keys
 */
object Hub {
    private const val FM1 = "felucca"
    private const val LOG_N = 200

    private val lock = Any()
    private val log = ArrayDeque<String>()
    var onLog: (() -> Unit)? = null

    private var mm: MidiManager? = null
    private var handler: Handler? = null
    private var fm1: MidiInputPort? = null
    private val open = HashMap<Int, Attached>()     // MidiDeviceInfo id -> what is open on it
    private val opening = HashSet<Int>()            // .. asked for, not open yet (a device listed and added both)

    private class Attached(val name: String, val dev: MidiDevice, val ports: List<AutoCloseable>, val mix: MidiMix?)

    fun lines(): List<String> = synchronized(log) { log.toList() }

    fun say(s: String) {
        synchronized(log) {
            log.addLast(s)
            while (log.size > LOG_N) log.removeFirst()
        }
        onLog?.invoke()
    }

    private val callback = object : MidiManager.DeviceCallback() {
        override fun onDeviceAdded(info: MidiDeviceInfo) = attach(info)
        override fun onDeviceRemoved(info: MidiDeviceInfo) = detach(info)
    }

    private val ticker = object : Runnable {
        override fun run() {
            synchronized(lock) { open.values.forEach { it.mix?.tick() } }
            handler?.postDelayed(this, 10)
        }
    }

    fun start(m: MidiManager, h: Handler) {
        mm = m
        handler = h
        say("hub: waiting for the FM-1 and controllers")
        @Suppress("DEPRECATION")
        m.registerDeviceCallback(callback, h)
        @Suppress("DEPRECATION")
        m.devices.forEach { attach(it) }
        h.post(ticker)
    }

    fun stop() {
        mm?.unregisterDeviceCallback(callback)
        handler?.removeCallbacks(ticker)
        synchronized(lock) {
            open.values.forEach { close(it) }
            open.clear()
            opening.clear()
            fm1 = null
        }
        mm = null
        handler = null
        say("hub: stopped")
    }

    private fun nameOf(info: MidiDeviceInfo): String {
        val p = info.properties
        return p.getString(MidiDeviceInfo.PROPERTY_NAME)
            ?: p.getString(MidiDeviceInfo.PROPERTY_PRODUCT)
            ?: "device ${info.id}"
    }

    private fun attach(info: MidiDeviceInfo) {
        val m = mm ?: return
        synchronized(lock) {
            if (info.type != MidiDeviceInfo.TYPE_USB || open.containsKey(info.id) || !opening.add(info.id)) return
        }
        m.openDevice(info, { dev ->
            synchronized(lock) { opening.remove(info.id) }
            if (dev == null) {
                say("! ${nameOf(info)}: cannot open")
                return@openDevice
            }
            synchronized(lock) { opened(info, dev) }
        }, handler)
    }

    private fun opened(info: MidiDeviceInfo, dev: MidiDevice) {
        val name = nameOf(info)
        val ports = ArrayList<AutoCloseable>()
        var mix: MidiMix? = null
        if (name.contains(FM1, ignoreCase = true)) {
            val inp = if (info.inputPortCount > 0) dev.openInputPort(0) else null
            inp?.let { ports.add(it); fm1 = it }
            if (info.outputPortCount > 0) dev.openOutputPort(0)?.let {
                it.connect(Parser { msg -> fromFm1(msg) })
                ports.add(it)
            }
            say("FM-1: $name")
        } else {
            if (name.contains("MIDI Mix", ignoreCase = true))
                mix = MidiMix(if (info.inputPortCount > 0) dev.openInputPort(0)?.also { ports.add(it) } else null)
            for (i in 0 until info.outputPortCount) dev.openOutputPort(i)?.let { port ->
                val mx = mix
                port.connect(Parser { msg -> synchronized(lock) { toFm1(name, mx, msg) } })
                ports.add(port)
            }
            say("+ $name" + if (mix != null) " (MIDI Mix: tracks 1-8)" else "")
        }
        open[info.id] = Attached(name, dev, ports, mix)
    }

    private fun detach(info: MidiDeviceInfo) {
        synchronized(lock) {
            val a = open.remove(info.id) ?: return
            if (a.name.contains(FM1, ignoreCase = true)) {
                fm1 = null
                say("FM-1 gone")
            } else say("- ${a.name}")
            close(a)
        }
    }

    private fun close(a: Attached) {
        a.ports.forEach { runCatching { it.close() } }
        runCatching { a.dev.close() }
    }

    /* a message from a controller: to the FM-1 (a MIDI Mix's translated) */
    private fun toFm1(from: String, mix: MidiMix?, msg: ByteArray) {
        val out = fm1 ?: return
        val s = msg[0].toInt() and 0xFF
        if (s == 0xF8 || s == 0xFE) return
        for (m in mix?.translate(msg) ?: listOf(msg))
            runCatching { out.send(m, 0, m.size) }.onFailure { say("! FM-1 send: ${it.message}") }
    }

    /* a message from the FM-1: the track it selects (CC102) and a track's notes (CC105), on channel 9 + track */
    private fun fromFm1(msg: ByteArray) {
        if (msg.size != 3) return
        val s = msg[0].toInt() and 0xFF
        val ch = s and 0x0F
        val cc = msg[1].toInt()
        if (s and 0xF0 != 0xB0 || ch < 8 || (cc != 102 && cc != 105)) return
        synchronized(lock) {
            for (a in open.values) a.mix?.let { if (cc == 102) it.show(ch - 8) else it.hit(ch - 8) }
        }
    }
}

/* an Akai MIDI Mix (factory mapping, all on channel 1) */
class MidiMix(private val led: MidiInputPort?) {
    companion object {
        val KNOBS = arrayOf(
            intArrayOf(16, 20, 24, 28, 46, 50, 54, 58),     // row 1
            intArrayOf(17, 21, 25, 29, 47, 51, 55, 59),     // row 2
            intArrayOf(18, 22, 26, 30, 48, 52, 56, 60))     // row 3
        val ROW_CC = intArrayOf(-1, 103, 104)
        val FADERS = intArrayOf(19, 23, 27, 31, 49, 53, 57, 61)
        val MUTES = intArrayOf(1, 4, 7, 10, 13, 16, 19, 22)  // notes (their LEDs: the same notes back)
        val ARMS = intArrayOf(3, 6, 9, 12, 15, 18, 21, 24)   // REC ARM: selects the track, its LED the one selected
        const val FLASH = 70L                                  // a note's flash, ms
    }

    private val muted = BooleanArray(8)
    private var sel = 0
    private val until = LongArray(8)                           // a strip's flash: until then (0: none)

    private fun note(n: Int, on: Boolean) {
        led?.let { runCatching { it.send(byteArrayOf(0x90.toByte(), n.toByte(), (if (on) 127 else 0).toByte()), 0, 3) } }
    }

    private fun arm(k: Int, on: Boolean) = note(ARMS[k], on)

    /* track n selected: its REC ARM LED lit, the others dark */
    fun show(n: Int) {
        sel = n
        for (k in 0 until 8) {
            until[k] = 0
            arm(k, k == n)
        }
    }

    /* a note of track n (a muted one plays none): its REC ARM flashes */
    fun hit(n: Int) {
        if (n !in 0 until 8) return
        if (until[n] == 0L) arm(n, n != sel)
        until[n] = SystemClock.uptimeMillis() + FLASH
    }

    fun tick() {
        val now = SystemClock.uptimeMillis()
        for (k in 0 until 8)
            if (until[k] != 0L && now >= until[k]) {
                until[k] = 0
                arm(k, k == sel)
            }
    }

    private fun cc(ch: Int, c: Int, v: Int) = byteArrayOf((0xB0 or ch).toByte(), c.toByte(), v.toByte())

    /* the FM-1 messages for one MIDI Mix message */
    fun translate(msg: ByteArray): List<ByteArray> {
        if (msg.size != 3) return emptyList()
        val type = msg[0].toInt() and 0xF0
        val d1 = msg[1].toInt()
        val d2 = msg[2].toInt()
        if (type == 0xB0) {
            for ((row, ccs) in KNOBS.withIndex()) {
                val n = ccs.indexOf(d1)
                if (n >= 0 && ROW_CC[row] >= 0) return listOf(cc(8 + n, ROW_CC[row], d2))
            }
            val n = FADERS.indexOf(d1)
            return if (n >= 0) listOf(cc(8 + n, 7, d2)) else emptyList()
        }
        if (type == 0x90 && d2 > 0) {
            var n = ARMS.indexOf(d1)
            if (n >= 0) {
                show(n)
                return listOf(cc(8 + n, 102, 127))
            }
            n = MUTES.indexOf(d1)
            if (n >= 0) {
                muted[n] = !muted[n]
                note(d1, muted[n])
                return listOf(cc(8 + n, 92, if (muted[n]) 127 else 0))
            }
        }
        return emptyList()
    }
}

/* a MIDI byte stream -> whole messages (running status kept, SysEx whole, real-time ones as they come) */
class Parser(private val out: (ByteArray) -> Unit) : MidiReceiver() {
    private var status = 0
    private val buf = ByteArray(3)
    private var n = 0
    private var need = 0
    private var sysex: ByteArrayOutputStream? = null

    private fun dataLen(s: Int) = when (s and 0xF0) {
        0xC0, 0xD0 -> 1
        0xF0 -> when (s) { 0xF1, 0xF3 -> 1; 0xF2 -> 2; else -> 0 }
        else -> 2
    }

    override fun onSend(data: ByteArray, offset: Int, count: Int, timestamp: Long) {
        for (i in offset until offset + count) {
            val x = data[i].toInt() and 0xFF
            when {
                x >= 0xF8 -> out(byteArrayOf(x.toByte()))
                x == 0xF0 -> { sysex = ByteArrayOutputStream().apply { write(x) }; status = 0 }
                x == 0xF7 -> sysex?.let { it.write(x); out(it.toByteArray()); sysex = null }
                sysex != null -> sysex!!.write(x)
                x >= 0x80 -> {
                    status = x
                    n = 0
                    need = dataLen(x)
                    buf[0] = x.toByte()
                    if (need == 0) {
                        out(byteArrayOf(buf[0]))
                        status = 0
                    }
                }
                status != 0 -> {
                    buf[1 + n++] = x.toByte()
                    if (n == need) {
                        out(buf.copyOf(1 + need))
                        n = 0
                        if (status >= 0xF0) status = 0
                    }
                }
            }
        }
    }
}
