package com.droidspaces.app.util

import android.content.Context
import android.os.Build
import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream

sealed class InstallationStep {
    data class DetectingArchitecture(val arch: String) : InstallationStep()
    data class CreatingDirectories(val path: String) : InstallationStep()
    data class CopyingBinary(val binary: String) : InstallationStep()
    data class SettingPermissions(val path: String) : InstallationStep()
    data class Verifying(val path: String) : InstallationStep()
    object Success : InstallationStep()
    data class Error(val message: String) : InstallationStep()
}

object BinaryInstaller {
    private const val INSTALL_PATH = Constants.INSTALL_PATH
    private const val DROIDSPACES_BINARY_NAME = Constants.DROIDSPACES_BINARY_NAME
    private const val BUSYBOX_BINARY_NAME = Constants.BUSYBOX_BINARY_NAME

    /**
     * Map Android architecture to binary name suffix
     */
    private fun getArchitectureSuffix(context: Context): String = DeviceArch.suffix(context)

    /**
     * Get droidspaces binary name for architecture
     */
    private fun getDroidspacesBinaryName(context: Context): String {
        return "droidspaces-${getArchitectureSuffix(context)}"
    }

    /**
     * Get busybox binary name for architecture
     */
    private fun getBusyboxBinaryName(context: Context): String {
        return "busybox-${getArchitectureSuffix(context)}"
    }


    /**
     * Get human-readable architecture name
     */
    fun getArchitectureName(context: Context): String = DeviceArch.displayName(context)

    /**
     * Install droidspaces binary with progress updates
     */
    suspend fun install(
        context: Context,
        onProgress: (InstallationStep) -> Unit
    ): Result<Unit> = withContext(Dispatchers.IO) {
        try {
            // Step 1: Detect architecture
            val arch = getArchitectureName(context)
            onProgress(InstallationStep.DetectingArchitecture(arch))

            val droidspacesBinaryName = getDroidspacesBinaryName(context)
            val busyboxBinaryName = getBusyboxBinaryName(context)

            // Always install to the canonical path. The daemon's g_self_path fix
            // means this is safe even while the daemon is running - the mv is
            // atomic and the daemon automatically re-execs the new binary.
            val droidspacesTargetPath = Constants.DROIDSPACES_BINARY_PATH
            val busyboxTargetPath = Constants.BUSYBOX_BINARY_PATH

            // Step 2: Create directories
            onProgress(InstallationStep.CreatingDirectories(INSTALL_PATH))
            val mkdirResult = Shell.cmd("mkdir -p $INSTALL_PATH").exec()
            if (!mkdirResult.isSuccess) {
                return@withContext Result.failure(
                    Exception("Failed to create directory: ${mkdirResult.err.joinToString()}")
                )
            }

            // Helper function to install a binary
            // Uses atomic move operation to avoid "text file busy" error when binary is running.
            // Strategy: Copy to temp file in same directory, then use mv (atomic on same filesystem).
            // This works even if the target binary is currently executing.
            fun installBinary(assetName: String, targetPath: String, displayName: String): Result<Unit> {
                onProgress(InstallationStep.CopyingBinary(displayName))
                val assetManager = context.assets
                val inputStream = assetManager.open("binaries/$assetName")

                // Write to a temp file in app cache first (we can write here without root)
                val tempFile = File("${context.cacheDir}/$assetName")
                FileOutputStream(tempFile).use { output ->
                    inputStream.copyTo(output)
                }
                inputStream.close()

                // The rename inside installFileDurably also dodges "text file busy" while the
                // daemon is running the old binary.
                onProgress(InstallationStep.SettingPermissions(targetPath))
                val installResult = ContainerManager.installFileDurably(tempFile.absolutePath, targetPath, "755")
                tempFile.delete()
                if (!installResult.isSuccess) {
                    return Result.failure(
                        Exception("Failed to install $displayName: ${(installResult.out + installResult.err).joinToString()}")
                    )
                }

                return Result.success(Unit)
            }

            // Step 3: Install droidspaces binary
            installBinary(droidspacesBinaryName, droidspacesTargetPath, "droidspaces")
                .getOrElse { error -> return@withContext Result.failure(error) }

            // Step 4: Install busybox binary
            installBinary(busyboxBinaryName, busyboxTargetPath, "busybox")
                .getOrElse { error -> return@withContext Result.failure(error) }

            // Step 5: Verification (scripts are handled by ModuleInstaller)

            onProgress(InstallationStep.Verifying("droidspaces and busybox"))
            val verifyDroidspaces = Shell.cmd("test -x $droidspacesTargetPath && echo 'verified' || echo 'verification_failed'").exec()
            val verifyBusybox = Shell.cmd("test -x $busyboxTargetPath && echo 'verified' || echo 'verification_failed'").exec()

            if (!verifyDroidspaces.isSuccess || !verifyDroidspaces.out.any { it.contains("verified") }) {
                return@withContext Result.failure(
                    Exception("Droidspaces binary verification failed: file is not executable")
                )
            }

            if (!verifyBusybox.isSuccess || !verifyBusybox.out.any { it.contains("verified") }) {
                return@withContext Result.failure(
                    Exception("Busybox binary verification failed: file is not executable")
                )
            }

            // Success
            onProgress(InstallationStep.Success)
            Result.success(Unit)

        } catch (e: Exception) {
            onProgress(InstallationStep.Error(e.message ?: "Unknown error"))
            Result.failure(e)
        }
    }

    /**
     * After a live binary swap, send SIGUSR2 to the running daemon
     * so it acknowledges the update (used for logging).
     */
    suspend fun signalDaemon(): Unit = withContext(Dispatchers.IO) {
        val pidResult = Shell.cmd("cat ${Constants.DAEMON_PID_FILE} 2>/dev/null").exec()
        if (pidResult.isSuccess && pidResult.out.isNotEmpty()) {
            val pid = pidResult.out[0].trim()
            if (pid.isNotEmpty()) {
                Shell.cmd("kill -USR2 $pid 2>/dev/null").exec()
            }
        }
    }

    /**
     * Check if binaries are already installed
     */
    suspend fun isInstalled(): Boolean = withContext(Dispatchers.IO) {
        val droidspacesResult = Shell.cmd("test -x $INSTALL_PATH/$DROIDSPACES_BINARY_NAME && echo 'installed' || echo 'not_installed'").exec()
        val busyboxResult = Shell.cmd("test -x $INSTALL_PATH/$BUSYBOX_BINARY_NAME && echo 'installed' || echo 'not_installed'").exec()
        val droidspacesOk = droidspacesResult.isSuccess && droidspacesResult.out.any { it.contains("installed") }
        val busyboxOk = busyboxResult.isSuccess && busyboxResult.out.any { it.contains("installed") }

        droidspacesOk && busyboxOk
    }
}

