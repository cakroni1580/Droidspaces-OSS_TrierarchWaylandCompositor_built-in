package com.droidspaces.app.wayland

import android.view.Surface

/**
 * JNI surface bridge for the Wayland compositor renderer.
 *
 * Lifecycle:
 *   nativeSurfaceCreated()   → starts EGL render thread (stops headless dispatch)
 *   nativeSurfaceDestroyed() → tears down EGL, restarts headless dispatch
 *                              (compositor keeps running, container clients unaffected)
 *
 * Input constants match pointer_input.c / WaylandTouchLayout.
 */
object WaylandSurface {

    /* Pointer action constants — mirror compositor pointer_input.c */
    const val ACTION_DOWN          = 0
    const val ACTION_MOVE          = 1
    const val ACTION_UP            = 2
    const val ACTION_POINTER_MOVE  = 6

    // ---- Surface lifecycle --------------------------------------------------

    external fun nativeSurfaceCreated(
        surface: Surface,
        resolutionPercent: Int,
        scalePercent: Int,
    )

    external fun nativeSurfaceDestroyed()

    external fun nativeOutputSizeChanged(
        width: Int,
        height: Int,
        resolutionPercent: Int,
        scalePercent: Int,
    )

    // ---- Input --------------------------------------------------------------

    external fun nativeOnPointerEvent(x: Float, y: Float, action: Int, timeMs: Int)
    external fun nativeOnPointerAxis(deltaX: Float, deltaY: Float, timeMs: Int)
    external fun nativeOnPointerRightClick(x: Float, y: Float, timeMs: Int)

    /** androidKeyCode: Android KeyEvent.KEYCODE_* — converted to Linux evdev in C. */
    external fun nativeOnKeyEvent(androidKeyCode: Int, isDown: Boolean, timeMs: Int)

    /**
     * Send a synthetic pointer-move to the center of the compositor output.
     * This triggers keyboard_focus_update() via the normal mutex-protected
     * compositor_pointer_event() path — establishing wl_keyboard.enter on
     * whichever surface is at the center. Safe to call before toolbar key presses.
     */
    external fun nativeEnsureFocus(timeMs: Int)

    external fun nativeSetCursorVisible(visible: Boolean)
    external fun nativeGetLogicalWidth(): Int

    external fun nativeGetLogicalHeight(): Int
}
