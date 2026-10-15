<!--
title: 卸载指南
section: Reference
order: 5
desc: 从 Android 和 Linux 上安全卸载 Droidspaces。彻底删除容器、后端数据、APK 和系统文件。
keywords: uninstall, droidspaces, remove, android, delete, rootfs, cleanup, container, runtime, backend, data
-->

> [!NOTE]
> 本页的翻译落后于英文版本，部分内容可能已过时。最新内容请看[英文页面](../Uninstallation.md)。

# 卸载指南

本指南将讲述如何从你的系统中安全、彻底地删除 Droidspaces。

---

## Android（应用）

请按照以下步骤操作，确保所有容器数据和后端文件都被删除。

1.  **停止容器**：确保所有正在运行的容器已从**容器**选项卡中停止。
2.  **删除容器**：
    - 前往**容器**选项卡。
    - 长按每个容器卡片，选择**卸载容器**。这将删除 rootfs 镜像及其配置。
3.  **删除后端数据**：
    - 使用具有 Root 权限的文件管理器或终端（如带有 Root 权限的 Termux），删除以下目录：
    ```bash
    su -c "rm -rf /data/local/Droidspaces"
    ```
4.  **卸载 APK**：从你的 Android 设置或启动器中卸载 Droidspaces 应用。
5.  **重启**：从你的 Root 管理器中禁用或移除 Magisk/KernelSU 的 `Droidspaces: Run-at-boot` 模块，然后重启以清除任何残留的 Droidspaces 相关内容。

---

## Linux（CLI）

1.  **停止容器**：停止所有正在运行的容器。
    ```bash
    sudo droidspaces --name=web,db stop
    ```
2.  **删除文件系统**：删除 Droidspaces 工作空间目录。这将删除残留的 PID 文件和日志。

    ```bash
    sudo rm -rf /var/lib/Droidspaces
    ```
3.  **删除二进制文件**：删除 `droidspaces` 二进制文件。
    ```bash
    sudo rm /usr/local/bin/droidspaces
    # 或者你安装它的任何位置
    ```
