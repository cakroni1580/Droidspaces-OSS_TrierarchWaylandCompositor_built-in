package com.droidspaces.app.ui.screen

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.repeatOnLifecycle
import com.droidspaces.app.ui.component.DsSnackbarHost
import com.droidspaces.app.ui.component.EmptyState
import com.droidspaces.app.ui.component.ErrorState
import com.droidspaces.app.ui.component.KernelUnsupportedState
import com.droidspaces.app.ui.component.RootUnavailableState
import com.droidspaces.app.ui.component.PullToRefreshWrapper
import com.droidspaces.app.ui.component.RunningContainerCard
import com.droidspaces.app.ui.util.AnimatedListEntry
import com.droidspaces.app.ui.util.showError
import com.droidspaces.app.ui.util.rememberAnimatedEntries
import com.droidspaces.app.ui.viewmodel.ContainerViewModel
import com.droidspaces.app.ui.viewmodel.SystemStatsViewModel
import com.droidspaces.app.util.AnlandUtils
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.sp
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.shape.RoundedCornerShape
import com.droidspaces.app.R
import com.droidspaces.app.util.Constants
import com.droidspaces.app.wayland.WaylandManager

/**
 * Control Panel screen - shows system stats and running containers.
 *
 * Note: This screen does NOT have its own PullToRefreshWrapper.
 * The parent ControlPanelTabContent provides the pull-to-refresh functionality.
 * This prevents double-wrapping issues that can cause UI glitches.
 */
@Composable
fun ControlPanelScreen(
    isBackendAvailable: Boolean,
    isRootAvailable: Boolean = true,
    isKernelSupported: Boolean = true,
    containerViewModel: ContainerViewModel,
    onNavigateToContainerDetails: (String) -> Unit = {},
    onNavigateToTerminal: (String) -> Unit = {},
    bottomBarInset: Dp = 0.dp,
    onNavigateToWaylandDisplay: () -> Unit = {},
    emptyStateBottomInset: Dp = 0.dp,
) {
    val context = LocalContext.current
    val snackbarHostState = remember { SnackbarHostState() }
    val scope = rememberCoroutineScope()
    val systemStatsViewModel: SystemStatsViewModel = viewModel()

    // Get running containers - derived from ViewModel state
    val runningContainers = containerViewModel.containerList.filter { it.isRunning }
    val entries = rememberAnimatedEntries(runningContainers) { it.name }

    val lifecycleOwner = LocalLifecycleOwner.current

    // Poll only while this screen is composed (i.e. the Panel tab is selected)
    // AND the app is in the foreground (Lifecycle STARTED).  repeatOnLifecycle
    // cancels the loop on background / tab-away and restarts it on return, so
    // re-opening the app on the Panel tab resumes polling without a tab switch.
    // Restarts whenever the running container set changes.
    LaunchedEffect(runningContainers) {
        lifecycleOwner.lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
            // A container that died on its own (poweroff inside it) is only noticed by
            // this poll, so it has to tell the list, or its card stays up with no stats.
            systemStatsViewModel.monitorContainers(runningContainers, onStopped = containerViewModel::refresh)
        }
    }

    val containerUsageMap = systemStatsViewModel.containerUsageMap

    Box(modifier = Modifier.fillMaxSize()) {
        // Show content based on root and backend availability
        // Using when instead of early return to prevent UI glitches during recomposition
        when {
            !isRootAvailable -> {
                RootUnavailableState(modifier = Modifier.padding(bottom = bottomBarInset))
            }
            !isBackendAvailable -> {
                ErrorState(modifier = Modifier.padding(bottom = bottomBarInset))
            }
            !isKernelSupported -> {
                KernelUnsupportedState(modifier = Modifier.padding(bottom = bottomBarInset))
            }
            else -> {
                if (entries.isEmpty()) {
                    EmptyState(
                        icon = Icons.Default.Dashboard,
                        title = context.getString(R.string.no_containers_running),
                        description = context.getString(R.string.start_container_first),
                        // Reserve the floating tab bar's space so the centered
                        // content sits in the visible region, not behind the bar.
                        modifier = Modifier.padding(bottom = bottomBarInset)
                    )
                } else {
                    Column(
                        modifier = Modifier
                            .fillMaxSize()
                            .verticalScroll(rememberScrollState())
                            .padding(horizontal = 16.dp)
                            // Each card carries its own 16dp gap (AnimatedListEntry) on top of this
                            .padding(top = 8.dp, bottom = bottomBarInset + 16.dp),
                    ) {
                        entries.forEach { entry ->
                            val container = entry.item
                            // The heartbeat reports the broker's socket only for a container
                            // booted with Anland, so it gates the pill, not the saved config.
                            val anlandSock = containerUsageMap[container.name]?.anlandSocket
                            // Keyed, so a card's state stays with its container when one leaves.
                            key(entry.key) {
                                AnimatedListEntry(entry) {
                                    RunningContainerCard(
                                        container = container,
                                        onEnter = {
                                            onNavigateToContainerDetails(container.name)
                                        },
                                        onTerminalClick = {
                                            onNavigateToTerminal(container.name)
                                        },
                                        anlandEnabled = anlandSock != null,
                                        onLaunchAnland = {
                                            if (anlandSock != null && !AnlandUtils.launchWindow(context, container.name, anlandSock))
                                                scope.showError(snackbarHostState, context.getString(R.string.anland_not_installed))
                                        },
                                        osInfo = containerUsageMap[container.name],
                                    )
                                }
                            }
                        }
                    }
                // Wayland display card — shown on arm64 when compositor is running
                if (Constants.isArm64 && WaylandManager.isRunning) {
                    WaylandDisplayCard(onClick = onNavigateToWaylandDisplay)

                }
            }
        }

        // Snackbar host (always present)
        DsSnackbarHost(
            hostState = snackbarHostState,
            modifier = Modifier.align(Alignment.BottomCenter).padding(bottom = bottomBarInset + 16.dp)
        )
    }
}

@Composable
private fun WaylandDisplayCard(onClick: () -> Unit) {
    val cardShape = RoundedCornerShape(20.dp)
    Surface(
        onClick   = onClick,
        shape     = cardShape,
        color     = MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.35f),
        border    = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.25f)),
        modifier  = Modifier.fillMaxWidth(),
        tonalElevation = 0.dp,
    ) {
        Row(
            modifier            = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 14.dp),
            verticalAlignment   = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(14.dp),
        ) {
            // Pulsing dot + icon
            Surface(
                shape = RoundedCornerShape(12.dp),
                color = MaterialTheme.colorScheme.primary.copy(alpha = 0.12f),
            ) {
                Icon(
                    imageVector        = Icons.Default.DesktopWindows,
                    contentDescription = null,
                    tint               = MaterialTheme.colorScheme.primary,
                    modifier           = Modifier.padding(10.dp).size(22.dp),
                )
            }

            Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                Text(
                    "Wayland Display",
                    style      = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.SemiBold,
                    color      = MaterialTheme.colorScheme.onSurface,
                )
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    // Live indicator dot
                    Surface(
                        shape = RoundedCornerShape(50),
                        color = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.size(6.dp),
                    ) {}
                    Text(
                        "Compositor active — tap to open",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        fontSize = 11.sp,
                    )
                }
            }

            Icon(
                imageVector        = Icons.Default.ChevronRight,
                contentDescription = null,
                tint               = MaterialTheme.colorScheme.primary.copy(alpha = 0.7f),
                modifier           = Modifier.size(20.dp),
            )
        }
    }
}
