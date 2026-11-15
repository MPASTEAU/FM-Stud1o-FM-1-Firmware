package fr.felucca.midihub

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.media.midi.MidiManager
import android.os.Handler
import android.os.HandlerThread
import android.os.IBinder
import android.os.PowerManager

/* the hub kept running with the screen off (a foreground service, the CPU held awake while it runs) */
class HubService : Service() {
    companion object {
        @Volatile var running = false
        private const val CHANNEL = "hub"
    }

    private var thread: HandlerThread? = null
    private var wake: PowerManager.WakeLock? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (running) return START_STICKY
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(CHANNEL, "FM-1 Hub", NotificationManager.IMPORTANCE_LOW))
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
        val n = Notification.Builder(this, CHANNEL)
            .setContentTitle("FM-1 Hub")
            .setContentText("controllers -> FM-1")
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setContentIntent(open)
            .setOngoing(true)
            .build()
        startForeground(1, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
        wake = getSystemService(PowerManager::class.java)
            .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "fm1hub:midi").apply { acquire() }
        val t = HandlerThread("midi-hub").apply { start() }
        thread = t
        Hub.start(getSystemService(MidiManager::class.java), Handler(t.looper))
        running = true
        return START_STICKY
    }

    override fun onDestroy() {
        running = false
        Hub.stop()
        thread?.quitSafely()
        wake?.takeIf { it.isHeld }?.release()
        super.onDestroy()
    }
}
