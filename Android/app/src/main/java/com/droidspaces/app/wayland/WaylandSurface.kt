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

    /** Pointer axis source (compatible with Trierarch API). */
    const val AXIS_SOURCE_FINGER   = 1

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

    // ---- Pointer input ------------------------------------------------------

    external fun nativeOnPointerEvent(
        x: Float,
        y: Float,
        action: Int,
        timeMs: Int,
    )

    /**
     * axisSource disediakan agar API kompatibel dengan Trierarch.
     * Native boleh mengabaikannya bila compositor belum membedakan
     * finger wheel / mouse wheel.
     */
    @JvmOverloads
    external fun nativeOnPointerAxis(
        deltaX: Float,
        deltaY: Float,
        timeMs: Int,
        axisSource: Int = AXIS_SOURCE_FINGER,
    )

    external fun nativeOnPointerRightClick(
        x: Float,
        y: Float,
        timeMs: Int,
    )

    /**
     * Sinkronkan posisi cursor fisik Android dengan mapper native.
     *
     * NOTE:
     * Dipakai oleh TouchpadController, TabletController dan
     * TwoFingerScroll agar seluruh stack input Trierarch dapat
     * dipakai tanpa modifikasi.
     */
    external fun nativeSetCursorPhysical(
        x: Float,
        y: Float,
    )

    // ---- Keyboard -----------------------------------------------------------

    /** Android KEYCODE_* → Linux evdev dilakukan di native. */
    external fun nativeOnKeyEvent(
        androidKeyCode: Int,
        isDown: Boolean,
        timeMs: Int,
    )

    /**
     * Release seluruh modifier (Shift/Ctrl/Alt/Meta) apabila
     * terjadi stuck key.
     */
    external fun nativeResetKeyboardState()

    /**
     * Send a synthetic pointer-move to establish wl_keyboard.enter.
     */
    external fun nativeEnsureFocus(timeMs: Int)

    // ---- Cursor -------------------------------------------------------------

    external fun nativeSetCursorVisible(visible: Boolean)

    // ---- Output -------------------------------------------------------------

    external fun nativeGetLogicalWidth(): Int

    external fun nativeGetLogicalHeight(): Int

    /**
     * Convenience wrapper agar kompatibel dengan Trierarch.
     */
    fun nativeGetOutputSize(): IntArray =
        intArrayOf(
            nativeGetLogicalWidth(),
            nativeGetLogicalHeight(),
        )

    /**
     * True bila compositor memiliki minimal satu client/toplevel aktif.
     * Saat ini fallback menggunakan ukuran output; implementasi native
     * dapat diganti nanti tanpa mengubah API Kotlin.
     */
    external fun nativeHasActiveClients(): Boolean
}
