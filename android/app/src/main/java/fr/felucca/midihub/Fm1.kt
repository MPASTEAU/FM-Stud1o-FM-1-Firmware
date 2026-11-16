package fr.felucca.midihub

/*
 * The FM-1's tracks over the editor protocol (web/EDITOR_PROTOCOL.md): which track is selected, whether it plays
 * SAMPLE / USR1, and making it so (the sampler's ÉCOUTER SUR FM-1: with MIDI IN SEL, the FM-1's default, the notes go
 * to the selected track).
 */
object Fm1 {
    private const val ENG_SAMPLE = 4
    private const val SET_USR1 = 5                    // SAMPLE's SET: PIANO and its aliases 0..4, USR1..3 5..7
    private var pE0 = -1                              // P_E0's id (INFO), SET's

    private fun v14(r: ByteArray, i: Int) = (r[i].toInt() or (r[i + 1].toInt() shl 7)) - 8192
    private fun v14(v: Int) = byteArrayOf(((v + 8192) and 0x7F).toByte(), (((v + 8192) shr 7) and 0x7F).toByte())

    private fun e0(): Int {
        if (pE0 < 0) {
            val r = Hub.request(1, byteArrayOf())     // INFO: version string, NENGINES, P_COUNT, G_COUNT, NSTEP, P_E0
            pE0 = r[r.indexOf(0) + 5].toInt()
        }
        return pE0
    }

    fun selected(): Int = Hub.request(27, byteArrayOf())[0].toInt()

    /* track k plays SAMPLE, SET USR1 */
    fun isSampler(k: Int): Boolean {
        val r = Hub.request(29, byteArrayOf(k.toByte()))   // TRACK_DUMP: track, engine, preset, P_COUNT x v14
        return r[1].toInt() == ENG_SAMPLE && v14(r, 3 + 2 * e0()) == SET_USR1
    }

    /* track k (selected) to SAMPLE (its first preset), SET USR1: its sound replaced, its notes kept */
    fun makeSampler(k: Int) {
        Hub.request(27, byteArrayOf(k.toByte()))
        Hub.request(8, byteArrayOf(ENG_SAMPLE.toByte(), 0), 5000)
        Hub.request(31, byteArrayOf(k.toByte(), e0().toByte()) + v14(SET_USR1))
    }
}
