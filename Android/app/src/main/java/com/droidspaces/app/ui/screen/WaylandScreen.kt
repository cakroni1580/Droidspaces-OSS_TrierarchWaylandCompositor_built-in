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
import android.content.Context
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.unit.IntSize
import kotlin.math.roundToInt

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
    // Modifier keys that remain held until tapped again.
    // Required for sequences such as:
    // ALT → X
    // CTRL → C
    // CTRL → ALT → T
    // Modifier keys that remain held until tapped again.
    var ctrlLocked by remember { mutableStateOf(false) }
    var altLocked by remember { mutableStateOf(false) }

    // Always release modifiers when the Wayland screen leaves composition.
    // Prevents stale CTRL/ALT state after Activity back/navigation.
    DisposableEffect(Unit) {
        onDispose {
            if (ctrlLocked) {
                sendModifierKey(KeyEvent.KEYCODE_CTRL_LEFT, false)
            }

            if (altLocked) {
                sendModifierKey(KeyEvent.KEYCODE_ALT_LEFT, false)
            }
        }
    }

    val view = LocalView.current
    // ── Persistent draggable FAB ──────────────────────────────────────

    val fabPreferences = remember(view) {
        view.context.getSharedPreferences(
            "wayland_fab_position",
            Context.MODE_PRIVATE
        )
    }

    val hasSavedFabPosition = remember(fabPreferences) {
        fabPreferences.contains("center_x") &&
            fabPreferences.contains("center_y")
    }

    var fabCenterX by remember(fabPreferences) {
        mutableStateOf(
            fabPreferences.getFloat("center_x", 0.88f)
        )
    }

    var fabCenterY by remember(fabPreferences) {
        mutableStateOf(
            fabPreferences.getFloat("center_y", 0.90f)
        )
    }

    var fabPositionInitialized by remember {
        mutableStateOf(hasSavedFabPosition)
    }

    var containerSize by remember {
        mutableStateOf(IntSize.Zero)
    }

    var fabSize by remember {
        mutableStateOf(IntSize.Zero)
    }

    // Keep current position accessible inside pointerInput.
    val currentFabCenterX = rememberUpdatedState(fabCenterX)
    val currentFabCenterY = rememberUpdatedState(fabCenterY)

    val statusInsetPx = WindowInsets.statusBars.getTop(density)
    val navigationInsetPx = WindowInsets.navigationBars.getBottom(density)

    // IME and navigation bar overlap, so use the larger inset.
    val bottomInsetPx = maxOf(
        navigationInsetPx,
        imeBottomPx
    )

    val usableHeightPx = (
        containerSize.height -
            statusInsetPx -
            bottomInsetPx
    ).coerceAtLeast(0)

    // Initialize at BottomEnd on first launch.
    LaunchedEffect(
        containerSize,
        fabSize,
        usableHeightPx
    ) {
        if (
            !fabPositionInitialized &&
            containerSize.width > 0 &&
            containerSize.height > 0 &&
            fabSize.width > 0 &&
            fabSize.height > 0 &&
            usableHeightPx > 0
        ) {
            val margin = with(density) { 16.dp.toPx() }

            fabCenterX = (
                containerSize.width - margin - fabSize.width / 2f
            ) / containerSize.width

            fabCenterY = (
                usableHeightPx - margin - fabSize.height / 2f
            ) / usableHeightPx

            fabPositionInitialized = true
        }
    }

    // Calculate current FAB coordinates relative to the root Box.
    val fabMaxX = (containerSize.width - fabSize.width)
        .coerceAtLeast(0)

    val fabMaxY = (
        statusInsetPx + usableHeightPx - fabSize.height
    ).coerceAtLeast(statusInsetPx)

    val fabLeft = (
        fabCenterX * containerSize.width - fabSize.width / 2f
    ).roundToInt().coerceIn(0, fabMaxX)

    val fabTop = (
        statusInsetPx +
            fabCenterY * usableHeightPx -
            fabSize.height / 2f
    ).roundToInt().coerceIn(statusInsetPx, fabMaxY)

    val fabPositionOffset = IntOffset(fabLeft, fabTop)
    val imeVisible = imeBottomPx > 0
    val imeOffsetDp = with(density) {
        imeBottomPx.toDp()
    }

    val insetsController = remember(view) {
        val activity = view.context as? Activity ?: return@remember null
        WindowCompat.getInsetsController(activity.window, view)
    }

    LaunchedEffect(imeVisible) {
        waylandLayout?.setImeAnimating(imeVisible)
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
        modifier = Modifier
        .fillMaxSize()
        .onSizeChanged {
            containerSize = it
        }
    ) {

        Column(
            modifier = Modifier
                .fillMaxSize()
                .windowInsetsPadding(
                    WindowInsets.statusBars
                       .union(WindowInsets.navigationBars)
                )
                .imePadding()
        ) {

            WaylandDisplayView(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth(),
                onViewReady = { waylandLayout = it }
            )


            if (isRunning && imeVisible) {
                WaylandKeyboardBar(
                   isFullscreen = isFullscreen,
                   isKeyboardVisible = isKeyboardVisible,
                   ctrlLocked = ctrlLocked,
                   altLocked = altLocked,
                   onFullscreenToggle = { isFullscreen = !isFullscreen },
                   onKeyboardToggle = {
                       if (isKeyboardVisible) {
                           waylandLayout?.hideKeyboard()
                       } else {
                           waylandLayout?.showKeyboard()
                       }
                       isKeyboardVisible = !isKeyboardVisible
                   },
                   onPaste = {
                       waylandLayout?.pasteClipboard()
                   }, 
                   onCtrlToggle = {
                      ctrlLocked = !ctrlLocked
                      sendModifierKey(
                           KeyEvent.KEYCODE_CTRL_LEFT,
                           ctrlLocked
                      )
                   },
                   onAltToggle = {
                      altLocked = !altLocked
                      sendModifierKey(
                           KeyEvent.KEYCODE_ALT_LEFT,
                           altLocked
                      )
                   },
                )
            }
        }


        // ── Draggable Wayland Keyboard FAB ─────────────────────────────────

        AnimatedVisibility(
            visible = isRunning && !imeVisible,
            modifier = Modifier
                .zIndex(10f)
                .offset {
                    fabPositionOffset
                }
        ) {
            FloatingActionButton(
                onClick = {
                    waylandLayout?.showKeyboard()
                    isKeyboardVisible = true
                },
                modifier = Modifier
                    .onSizeChanged {
                        fabSize = it
                    }
                    .pointerInput(
                        containerSize,
                        fabSize,
                        statusInsetPx,
                        usableHeightPx
                    ) {
                        detectDragGestures(
                            onDragEnd = {
                                fabPreferences.edit()
                                    .putFloat("center_x", fabCenterX)
                                    .putFloat("center_y", fabCenterY)
                                    .apply()
                            },
                            onDragCancel = {
                                fabPreferences.edit()
                                    .putFloat("center_x", fabCenterX)
                                    .putFloat("center_y", fabCenterY)
                                    .apply()
                            }
                        ) { change, dragAmount ->

                            change.consume()

                            if (
                                containerSize.width <= 0 ||
                                usableHeightPx <= 0
                            ) return@detectDragGestures

                            val maxX = (
                                containerSize.width - fabSize.width
                            ).coerceAtLeast(0)

                            val maxY = (
                                statusInsetPx + usableHeightPx - fabSize.height
                            ).coerceAtLeast(statusInsetPx)

                            // Reconstruct current pixel position.
                            val currentLeft = (
                                currentFabCenterX.value * containerSize.width -
                                    fabSize.width / 2f
                            ).roundToInt().coerceIn(0, maxX)

                            val currentTop = (
                                statusInsetPx +
                                    currentFabCenterY.value * usableHeightPx -
                                    fabSize.height / 2f
                            ).roundToInt().coerceIn(statusInsetPx, maxY)

                            // Apply drag and clamp to usable host area.
                            val newLeft = (
                                currentLeft + dragAmount.x.roundToInt()
                            ).coerceIn(0, maxX)

                            val newTop = (
                                currentTop + dragAmount.y.roundToInt()
                            ).coerceIn(statusInsetPx, maxY)

                            // Convert pixel coordinates into normalized position.
                            fabCenterX = (
                                (newLeft + fabSize.width / 2f) /
                                   containerSize.width
                            ).coerceIn(0f, 1f)

                            fabCenterY = (
                                (newTop - statusInsetPx + fabSize.height / 2f) /
                                   usableHeightPx
                            ).coerceIn(0f, 1f)
                       }
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
    ctrlLocked: Boolean,
    altLocked: Boolean,
    onFullscreenToggle: () -> Unit,
    onKeyboardToggle: () -> Unit,
    onPaste: () -> Unit,
    onCtrlToggle: () -> Unit,
    onAltToggle: () -> Unit,
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

                WlTextKey("ESC", KeyEvent.KEYCODE_ESCAPE)
                WlTextKey("TAB", KeyEvent.KEYCODE_TAB)
                WlModifierKey(
                    label = "CTRL",
                    locked = ctrlLocked,
                    onClick = onCtrlToggle
                )

                WlModifierKey(
                    label = "ALT",
                    locked = altLocked,
                    onClick = onAltToggle
                )

                VerticalDivider(
                    modifier = Modifier.height(26.dp),
                    color = MaterialTheme.colorScheme.outlineVariant
                )

                WlIconKey(
                    icon = Icons.Default.ContentPaste,
                    desc = "Paste",
                    onClick = onPaste
                )

                VerticalDivider(
                    modifier = Modifier.height(26.dp),
                    color = MaterialTheme.colorScheme.outlineVariant
                )

                WlIconKey(Icons.Default.KeyboardArrowUp, "↑", KeyEvent.KEYCODE_DPAD_UP)
                WlIconKey(Icons.Default.KeyboardArrowDown, "↓", KeyEvent.KEYCODE_DPAD_DOWN)
                WlIconKey(Icons.AutoMirrored.Filled.KeyboardArrowLeft, "←", KeyEvent.KEYCODE_DPAD_LEFT)
                WlIconKey(Icons.AutoMirrored.Filled.KeyboardArrowRight, "→", KeyEvent.KEYCODE_DPAD_RIGHT)

                VerticalDivider(
                    modifier = Modifier.height(26.dp),
                    color = MaterialTheme.colorScheme.outlineVariant
                )

                WlIconKey(
                   icon = if (isKeyboardVisible)
                       Icons.Default.KeyboardHide
                   else Icons.Default.Keyboard,
                   desc = "Toggle keyboard",
                   onClick = onKeyboardToggle
                )    
            }
        }
    }
}

// ── Key button helpers ───────────────────────────────────────────────────────

@Composable
private fun RowScope.WlModifierKey(
    label: String,
    locked: Boolean,
    onClick: () -> Unit,
) {
    TextButton(
        onClick = onClick,
        modifier = Modifier
            .weight(1f)
            .fillMaxHeight(),
        shape = RoundedCornerShape(8.dp),
        contentPadding = PaddingValues(0.dp),
        colors = ButtonDefaults.textButtonColors(
            containerColor = if (locked) {
                MaterialTheme.colorScheme.primaryContainer
            } else {
                androidx.compose.ui.graphics.Color.Transparent
            },
            contentColor = if (locked) {
                MaterialTheme.colorScheme.onPrimaryContainer
            } else {
                MaterialTheme.colorScheme.onSurface
            }
        )
    ) {
        Text(
            text = if (locked) "$label •" else label,
            fontSize = 11.sp,
            fontWeight = FontWeight.SemiBold
        )
    }
}

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

private fun sendModifierKey(
    keyCode: Int,
    pressed: Boolean
) {
    val t = (SystemClock.uptimeMillis() and 0x7FFF_FFFFL).toInt()

    WaylandSurface.nativeEnsureFocus(t)
    WaylandSurface.nativeOnKeyEvent(
        keyCode,
        pressed,
        t
    )
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
