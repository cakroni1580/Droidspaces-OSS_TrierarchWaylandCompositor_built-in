package com.droidspaces.app.ui.screen

import android.app.Activity
import android.os.SystemClock
import android.view.KeyEvent
import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowLeft
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.droidspaces.app.wayland.*
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.IntOffset
import androidx.compose.foundation.layout.offset

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun WaylandScreen(onNavigateBack: () -> Unit) {

    val density = LocalDensity.current

    val ime = WindowInsets.ime
    val imeBottomPx = with(density) {
        ime.getBottom(this)
    }

    val isRunning = WaylandManager.isRunning
    var isFullscreen by remember { mutableStateOf(false) }
    var isKeyboardVisible by remember { mutableStateOf(false) }
    var waylandLayout: WaylandDisplayLayout? by remember { mutableStateOf(null) }

    val view = LocalView.current
    val imeVisible = imeBottomPx > 0
    val imeOffsetDp = with(density) {
        imeBottomPx.toDp()
    }

    val insetsController = remember(view) {
        val activity = view.context as? Activity ?: return@remember null
        WindowCompat.getInsetsController(activity.window, view)
    }

    LaunchedEffect(isFullscreen) {
        insetsController?.let { ctrl ->
            if (isFullscreen) {
                ctrl.systemBarsBehavior =
                    WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                ctrl.hide(WindowInsetsCompat.Type.systemBars())
            } else {
                ctrl.show(WindowInsetsCompat.Type.systemBars())
            }
        }
    }

    DisposableEffect(insetsController) {
        onDispose {
            insetsController?.show(WindowInsetsCompat.Type.systemBars())
        }
    }

    BackHandler {
        if (isFullscreen) isFullscreen = false else onNavigateBack()
    }

    Box(
        modifier = Modifier.fillMaxSize()
    ) {

        Column(
            modifier = Modifier
                .fillMaxSize()
                .windowInsetsPadding(
                    WindowInsets.statusBars.union(
                        WindowInsets.navigationBars
                    )
                )
        ) {

            WaylandDisplayView(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth()
                    .windowInsetsPadding(
                        WindowInsets.ime
                    ),
                onViewReady = { waylandLayout = it }
            )


            AnimatedVisibility(
                visible = isRunning && imeVisible
            ) {
                WaylandKeyboardBar(
                    isFullscreen = isFullscreen,
                    isKeyboardVisible = isKeyboardVisible,
                    onFullscreenToggle = { isFullscreen = !isFullscreen },
                    onKeyboardToggle = {
                        if (isKeyboardVisible) {
                            waylandLayout?.hideKeyboard()
                        } else {
                            waylandLayout?.showKeyboard()
                        }
                        isKeyboardVisible = !isKeyboardVisible
                    },
                    onNavigateBack = onNavigateBack
                )
            }
        }


        // JANGAN dipindah.
        // FAB tetap overlay seperti implementasi awal.
        AnimatedVisibility(
            visible = isRunning && !imeVisible,
            modifier = Modifier
                .align(Alignment.BottomEnd)
                .navigationBarsPadding()
                .padding(16.dp)
        ) {
            FloatingActionButton(
                onClick = {
                    waylandLayout?.showKeyboard()
                    isKeyboardVisible = true
                }
            ) {
                Icon(
                    Icons.Default.Keyboard,
                    contentDescription = "Show keyboard"
                )
            }
        }
    }
}

// ── Bottom keyboard bar ──────────────────────────────────────────────────────

@Composable
private fun WaylandKeyboardBar(
    isFullscreen: Boolean,
    isKeyboardVisible: Boolean,
    onFullscreenToggle: () -> Unit,
    onKeyboardToggle: () -> Unit,
    onNavigateBack: () -> Unit,
) {
    Surface(
        color = MaterialTheme.colorScheme.surfaceContainerLow,
        tonalElevation = 0.dp,
    ) {
        Column {

            HorizontalDivider(
                thickness = 0.5.dp,
                color = MaterialTheme.colorScheme.outlineVariant
            )

            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(52.dp)
                    .padding(horizontal = 4.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(2.dp),
            ) {

                IconButton(
                    onClick = onNavigateBack,
                    modifier = Modifier.size(44.dp)
                ) {
                    Icon(
                        Icons.AutoMirrored.Filled.ArrowBack,
                        contentDescription = "Back",
                        modifier = Modifier.size(20.dp)
                    )
                }

                VerticalDivider(
                    modifier = Modifier.height(26.dp),
                    color = MaterialTheme.colorScheme.outlineVariant
                )

                WlTextKey("ESC", KeyEvent.KEYCODE_ESCAPE)
                WlTextKey("TAB", KeyEvent.KEYCODE_TAB)
                WlTextKey("CTRL", KeyEvent.KEYCODE_CTRL_LEFT)
                WlTextKey("ALT", KeyEvent.KEYCODE_ALT_LEFT)

                WlIconKey(
                    icon = if (isKeyboardVisible)
                        Icons.Default.KeyboardHide
                    else Icons.Default.Keyboard,
                    desc = "Toggle keyboard",
                    onClick = onKeyboardToggle
                )

                VerticalDivider(
                    modifier = Modifier.height(26.dp),
                    color = MaterialTheme.colorScheme.outlineVariant
                )

                WlIconKey(Icons.Default.KeyboardArrowUp, "↑", KeyEvent.KEYCODE_DPAD_UP)
                WlIconKey(Icons.Default.KeyboardArrowDown, "↓", KeyEvent.KEYCODE_DPAD_DOWN)
                WlIconKey(Icons.AutoMirrored.Filled.KeyboardArrowLeft, "←", KeyEvent.KEYCODE_DPAD_LEFT)
                WlIconKey(Icons.AutoMirrored.Filled.KeyboardArrowRight, "→", KeyEvent.KEYCODE_DPAD_RIGHT)
            }
        }
    }
}

// ── Key button helpers ───────────────────────────────────────────────────────

@Composable
private fun RowScope.WlTextKey(label: String, keyCode: Int) {
    TextButton(
        onClick = { sendKey(keyCode) },
        modifier = Modifier
            .weight(1f)
            .fillMaxHeight(),
        shape = RoundedCornerShape(8.dp),
        contentPadding = PaddingValues(0.dp),
    ) {
        Text(
            label,
            fontSize = 11.sp,
            fontWeight = FontWeight.SemiBold
        )
    }
}

@Composable
private fun RowScope.WlIconKey(
    icon: ImageVector,
    desc: String,
    keyCode: Int? = null,
    onClick: (() -> Unit)? = null,
) {
    IconButton(
        onClick = onClick ?: { if (keyCode != null) sendKey(keyCode) },
        modifier = Modifier
            .weight(1f)
            .fillMaxHeight()
    ) {
        Icon(icon, desc, modifier = Modifier.size(20.dp))
    }
}

private fun sendKey(keyCode: Int) {
    val t = (SystemClock.uptimeMillis() and 0x7FFF_FFFFL).toInt()
    WaylandSurface.nativeEnsureFocus(t)
    WaylandSurface.nativeOnKeyEvent(keyCode, true, t)
    WaylandSurface.nativeOnKeyEvent(keyCode, false, t + 1)
}

// ── Compositor-off placeholder ───────────────────────────────────────────────

@Composable
private fun CompositorOffPlaceholder(onNavigateBack: () -> Unit) {
    Column(
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(16.dp),
        modifier = Modifier.padding(32.dp),
    ) {
        Icon(
            Icons.Default.DesktopWindows,
            null,
            modifier = Modifier.size(48.dp),
            tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f),
        )
        Text(
            "Wayland compositor is not running",
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.SemiBold
        )
        Text(
            "Enable it in Settings → Wayland Compositor, then come back here.",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Spacer(Modifier.height(8.dp))
        OutlinedButton(
            onClick = onNavigateBack,
            shape = MaterialTheme.shapes.medium
        ) {
            Text("Go Back", fontWeight = FontWeight.SemiBold)
        }
    }
}
