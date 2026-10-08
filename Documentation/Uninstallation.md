<!--
title: Uninstallation
section: Reference
order: 5
desc: Remove Droidspaces from Android and Linux: containers, backend data, the APK and system files.
keywords: uninstall, droidspaces, remove, android, delete, rootfs, cleanup, container, runtime, backend, data
-->

# Uninstallation guide

How to remove Droidspaces completely.

---

## Android (app)

These steps remove all container data and backend files.

1.  **Stop containers**: stop every running container from the **Containers** tab.
2.  **Delete containers**:
    - Go to the **Containers** tab.
    - Long-press each container card and select **Uninstall Container**. This deletes the rootfs image and its configuration.
3.  **Remove backend data**:
    - With a root file manager or a terminal (Termux with root access, for example), delete this directory:
    ```bash
    su -c "rm -rf /data/local/Droidspaces"
    ```
4.  **Uninstall the APK**: uninstall the Droidspaces app from Android settings or your launcher.
5.  **Reboot**: disable or remove the Magisk/KernelSU `Droidspaces: Run-at-boot` module in your root manager, then reboot to clear anything Droidspaces left behind.

---

## Linux (CLI)

1.  **Stop containers**: stop every running container.
    ```bash
    sudo droidspaces --name=web,db stop
    ```
2.  **Remove the workspace**: delete the Droidspaces workspace directory. This removes stale PID files and logs.

    ```bash
    sudo rm -rf /var/lib/Droidspaces
    ```
3.  **Remove the binary**: delete the `droidspaces` binary.
    ```bash
    sudo rm /usr/local/bin/droidspaces
    # Or wherever you installed it
    ```
