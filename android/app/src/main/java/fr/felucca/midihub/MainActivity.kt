package fr.felucca.midihub

import android.Manifest
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Typeface
import android.os.Bundle
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

/* start / stop the hub, and what it sees (devices in and out) */
class MainActivity : Activity() {
    private lateinit var text: TextView
    private lateinit var scroll: ScrollView
    private lateinit var button: Button

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        button = Button(this).apply { setOnClickListener { toggle() } }
        text = TextView(this).apply {
            typeface = Typeface.MONOSPACE
            textSize = 13f
            setPadding(32, 16, 32, 16)
        }
        scroll = ScrollView(this).apply { addView(text) }
        setContentView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            fitsSystemWindows = true
            addView(button, LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT))
            addView(scroll, LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f))
        })
        if (checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)
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
