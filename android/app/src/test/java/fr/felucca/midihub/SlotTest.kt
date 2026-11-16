package fr.felucca.midihub

import org.junit.Assert.assertEquals
import org.junit.Test
import java.io.File
import kotlin.math.PI
import kotlin.math.sin

/* the slot as tools/sampleio.py builds it: the same ADPCM and header bytes (SLOT_REF: its output for this signal) */
class SlotTest {
    @Test
    fun sameAsSampleio() {
        val n = 5000
        val s = ShortArray(n) { (sin(2 * PI * 440 * it / 22050.0) * 30000 * (1 - it / n.toDouble())).toInt().toShort() }
        val data = Slot.encode(s)
        val hdr = Slot.header("TEST", n, data, 60)
        val ref = System.getenv("SLOT_REF") ?: return
        assertEquals(File("$ref.bin").readBytes().toList(), data.toList())
        assertEquals(File("$ref.hdr").readBytes().toList(), hdr.toList())
        val d = Slot.decode(data, n)
        var err = 0.0
        for (i in 64 until n) err = maxOf(err, kotlin.math.abs((d[i] - s[i]).toDouble()))
        assert(err < 3000) { "err $err" }
    }
}
