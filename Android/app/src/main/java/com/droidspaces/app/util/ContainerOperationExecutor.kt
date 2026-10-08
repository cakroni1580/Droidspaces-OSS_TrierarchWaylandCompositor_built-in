package com.droidspaces.app.util

import android.os.Handler
import android.os.Looper
import android.util.Log
import com.topjohnwu.superuser.CallbackList
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import kotlin.coroutines.resume

/**
 * Executes container operations (start/stop/restart) with TRUE real-time output streaming.
 *
 * Uses libsu's CallbackList API to receive each line of output as the binary produces it.
 * Each line is immediately dispatched to the logger via logImmediate() - no coroutines,
 * no fire-and-forget detached scopes, no race conditions.
 *
 * Flow: Binary stdout → CallbackList.onAddElement() [main thread] → logImmediate() → TerminalConsole
 */
object ContainerOperationExecutor {
    // Pre-compiled error pattern for efficient matching
    private val errorPattern = Regex("""(?i)(error|failed|fail)""")

    // Shared main thread Handler for guaranteed-ordered final log delivery
    private val mainHandler = Handler(Looper.getMainLooper())

    /**
     * Execute a container command with real-time output streaming.
     *
     * Each line from the binary appears in the terminal immediately as it's printed.
     * The coroutine blocks on exec() until the process exits, but output streams in real-time
     * via CallbackList's onAddElement() callback - which libsu calls on the main thread.
     *
     * We call logger.logImmediate() synchronously inside onAddElement instead of spinning up
     * MainScope().launch coroutines, which were detached, untracked, and caused the terminal
     * to cut off before all lines were flushed to the UI.
     *
     * Final log lines ("Command executed", operationCompletedMessage) are posted via
     * Handler.post() - which enqueues them AFTER any already-pending onAddElement callbacks
     * in the main thread's message queue. This guarantees they always appear at the bottom,
     * even if libsu's reader thread posted some onAddElement calls just before exec() returned.
     *
     * @param operationCompletedMessage Optional message to log after success.
     *        Logged in guaranteed order after all binary output, before the function returns.
     *        Pass null to skip (e.g. for internal checks).
     */
    suspend fun executeCommand(
        command: String,
        operation: String, // "start", "stop", "restart", "check"
        logger: ContainerLogger,
        skipHeader: Boolean = false,
        operationCompletedMessage: String? = null
    ) = withContext(Dispatchers.IO) {
        try {
            if (!skipHeader) {
                logger.i("Starting $operation operation...")
                logger.i("Command: $command")
                logger.i("")
            }

            val lastLineWasEmpty = java.util.concurrent.atomic.AtomicBoolean(false)

            // onAddElement is called by libsu on the main thread as each line arrives.
            // We call logImmediate() directly - synchronous, no coroutines, no races.
            val callbackList = object : CallbackList<String>() {
                override fun onAddElement(s: String?) {
                    s ?: return
                    val trimmed = s.trim()
                    if (trimmed.isEmpty()) {
                        logger.logImmediate(Log.INFO, "")
                        lastLineWasEmpty.set(true)
                    } else {
                        if (errorPattern.containsMatchIn(trimmed)) {
                            logger.logImmediate(Log.ERROR, s)
                        } else {
                            logger.logImmediate(Log.INFO, s)
                        }
                        lastLineWasEmpty.set(false)
                    }
                }
            }

            // exec() blocks on IO until the process exits.
            val result = Shell.cmd("$command 2>&1").to(callbackList).exec()

            // After exec() returns, libsu may still have onAddElement callbacks pending
            // in the main thread's Handler queue (posted by libsu's internal reader thread
            // just before the process exited). Using withContext(Main.immediate) here would
            // race with those pending callbacks.
            //
            // Instead, we post via Handler.post() which enqueues our block AFTER everything
            // already in the main thread's message queue - including any pending onAddElement
            // callbacks. This guarantees our final lines always appear at the very bottom.
            suspendCancellableCoroutine<Unit> { continuation ->
                mainHandler.post {
                    if (!lastLineWasEmpty.get()) {
                        logger.logImmediate(Log.INFO, "")
                    }
                    if (result.isSuccess) {
                        logger.logImmediate(Log.INFO, "Command executed (exit code: ${result.code})")
                        if (operationCompletedMessage != null) {
                            logger.logImmediate(Log.INFO, "")
                            logger.logImmediate(Log.INFO, operationCompletedMessage)
                        }
                    } else {
                        logger.logImmediate(Log.ERROR, "Command failed (exit code: ${result.code})")
                    }
                    continuation.resume(Unit)
                }
            }

            result.isSuccess
        } catch (e: Exception) {
            logger.e("Exception executing command: ${e.message}")
            logger.e(e.stackTraceToString())
            false
        }
    }
}
