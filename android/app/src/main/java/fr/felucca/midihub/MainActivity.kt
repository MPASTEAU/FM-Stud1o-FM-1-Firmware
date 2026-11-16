package fr.felucca.midihub

import android.Manifest
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Typeface
import android.os.Bundle
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

/* two tabs: HUB (start / stop the hub, and what it sees: devices in and out), SAMPLER (SamplerView) */
class MainActivity : Activity() {
    private lateinit var text: TextView
    private lateinit var scroll: ScrollView
    private lateinit var button: Button
    private lateinit var hub: LinearLayout
    private lateinit var sampler: SamplerView

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        button = Button(this).apply { setOnClickListener { toggle() } }
        text = TextView(this).apply {
            typeface = Typeface.MONOSPACE
            textSize = 13f
            setPadding(32, 16, 32, 16)
        }
        scroll = ScrollView(this).apply { addView(text) }
        hub = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(button, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))
            addView(scroll, LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f))
        }
        sampler = SamplerView(this).apply { visibility = View.GONE }
        val tabs = LinearLayout(this).apply {
            addView(Button(context).apply { text = "HUB"; setOnClickListener { show(false) } },
                LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
            addView(Button(context).apply { text = "SAMPLER"; setOnClickListener { show(true) } },
                LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
        }
        setContentView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            fitsSystemWindows = true
            addView(tabs, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))
            addView(hub, LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f))
            addView(sampler, LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f))
        })
        val want = arrayOf(Manifest.permission.POST_NOTIFICATIONS, Manifest.permission.RECORD_AUDIO)
            .filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
        if (want.isNotEmpty()) requestPermissions(want.toTypedArray(), 1)
        if (!HubService.running) startForegroundService(Intent(this, HubService::class.java))
    }

    override fun onResume() {
        super.onResume()
        Hub.onLog = { runOnUiThread { refresh() } }
        refresh()
    }

    override fun onPause() {
        Hub.onLog = null
        super.onPause()
    }

    private fun show(s: Boolean) {
        hub.visibility = if (s) View.GONE else View.VISIBLE
        sampler.visibility = if (s) View.VISIBLE else View.GONE
    }

    private fun toggle() {
        val i = Intent(this, HubService::class.java)
        if (HubService.running) stopService(i) else startForegroundService(i)
        text.postDelayed({ refresh() }, 200)
    }

    private fun refresh() {
        button.text = if (HubService.running) "STOP" else "START"
        text.text = Hub.lines().joinToString("\n")
        scroll.post { scroll.fullScroll(ScrollView.FOCUS_DOWN) }
    }
}
