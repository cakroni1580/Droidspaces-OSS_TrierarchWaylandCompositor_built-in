package com.droidspaces.app.ui.viewmodel

import android.app.Application
import android.util.Log
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.lifecycle.AndroidViewModel
import com.droidspaces.app.util.AndroidSystemStatsCollector
import com.droidspaces.app.util.ContainerInfo
import com.droidspaces.app.util.ContainerOSInfoManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext

/**
 * Holds host + per-container stats and exposes the polling loops as suspend
 * functions.  The loops are NOT self-managed here: the screen drives them via
 * repeatOnLifecycle(STARTED), so polling runs only while the Panel tab is
 * actually on screen AND the app is in the foreground, and is cancelled
 * structurally when either stops being true.  This avoids the old
 * ProcessLifecycleOwner observer, which stopped on background but never
 * restarted on return (polling stayed dead until a tab switch re-composed it).
 */
class SystemStatsViewModel(application: Application) : AndroidViewModel(application) {

    companion object {
        private const val TAG = "SystemStatsViewModel"
        private const val SYSTEM_INTERVAL_MS = 2000L
        private const val CONTAINER_INTERVAL_MS = 2000L
    }

    var systemUsage by mutableStateOf(AndroidSystemStatsCollector.SystemUsage())
        private set

    // Per-container OS info (containerName -> OSInfo) — single source of truth for all UI
    var containerUsageMap = mutableStateMapOf<String, ContainerOSInfoManager.OSInfo>()
        private set

    /**
     * Poll host system usage every [SYSTEM_INTERVAL_MS] until the caller's
     * coroutine is cancelled (e.g. the screen leaves STARTED).
     */
    suspend fun monitorSystem() {
        while (true) {
            try {
                systemUsage = withContext(Dispatchers.IO) {
                    AndroidSystemStatsCollector.collectUsage()
                }
            } catch (e: Exception) {
                Log.e(TAG, "Failed to collect system usage", e)
            }
            delay(SYSTEM_INTERVAL_MS)
        }
    }

    /**
     * Poll OS info for every running container until cancelled.  Returns
     * immediately when nothing is running; the caller re-invokes with a fresh
     * list whenever the running set changes.
     */
    suspend fun monitorContainers(containers: List<ContainerInfo>) {
        val running = containers.filter { it.isRunning }
        if (running.isEmpty()) return

        while (true) {
            running.forEach { container ->
                try {
                    val isAlive = withContext(Dispatchers.IO) {
                        com.droidspaces.app.util.ContainerManager
                            .checkContainerStatus(container.name).first
                    }
                    if (!isAlive) {
                        // Kill all terminal sessions for this container
                        val ctx = getApplication<Application>()
                        ctx.startService(
                            android.content.Intent(ctx, com.droidspaces.app.service.TerminalSessionService::class.java).apply {
                                action = com.droidspaces.app.service.TerminalSessionService.ACTION_STOP_CONTAINER_SESSIONS
                                putExtra(com.droidspaces.app.service.TerminalSessionService.EXTRA_CONTAINER_NAME, container.name)
                            }
                        )
                        containerUsageMap.remove(container.name)
                        return@forEach
                    }
                    val osInfo = withContext(Dispatchers.IO) {
                        ContainerOSInfoManager.getOSInfo(
                            containerName = container.name,
                            useCache = false,
                            appContext = getApplication()
                        )
                    }
                    containerUsageMap[container.name] = osInfo
                } catch (e: Exception) {
                    Log.e(TAG, "Failed to collect info for ${container.name}", e)
                }
            }
            delay(CONTAINER_INTERVAL_MS)
        }
    }

    fun clearContainerUsage(containerName: String) {
        containerUsageMap.remove(containerName)
    }
}
