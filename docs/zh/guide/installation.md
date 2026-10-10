# 安装、升级与恢复

[返回中文文档](../README.md) · [English](../../guide/installation.md)

本指南对应当前 Manager 和 ksud 的实现。ZySU 使用 LKM；安装 APK 只是安装管理器，还需要把包含对应内核模块的启动镜像部署到设备。

## 安装前确认

- 设备为 ARM64，Manager / userspace 要求 Android 12（API 31）及以上。
- 内核至少为 Linux 6.1。现成模块的 KMI 为 `android14-6.1`、`android15-6.6`、`android16-6.12`、`android17-6.18`。
- 能通过设备支持的方式写入启动分区，通常需要解锁 bootloader；保留与当前固件完全匹配的原始镜像及恢复方法。
- 明确镜像对应的分区和 A/B 槽位。不要只凭 Android 版本选 KMI，也不要拿其他固件版本的镜像直接刷入。

可用以下只读命令记录设备信息：

```sh
adb shell uname -r
adb shell getprop ro.product.cpu.abi
adb shell getprop ro.build.version.sdk
adb shell getprop ro.boot.slot_suffix
```

系统版本满足最低要求不代表内核兼容。上述四个 KMI 是构建目标，不能据此认定所有厂商设备都经过验证。

## 获取管理器

从 [ZySU Releases](https://github.com/wuluoy-creator/ZySU/releases) 下载 Manager APK。自行编译参见[构建指南](workflow-build.md)；Actions 中完整 APK 位于 `Manager-arm64-v8a`，`Manager-gradle-arm64-v8a` 是待重打包的中间产物。

安装后，从首页状态区域进入安装页。Manager 根据是否有 root 和 A/B 槽位显示可用安装方式。

## 普通 LKM 镜像修补

这是 `ksud boot-patch` 路径：将 ksuinit 和 LKM 放入 ramdisk，在早期启动阶段加载模块并移交原始 init。

1. 准备当前固件的原始 `boot.img` 或 `init_boot.img`，按照设备的 ramdisk 所在分区选择。
2. 在安装页选择本地文件；可使用内置 LKM，也可选取明确匹配目标内核的自定义 `.ko`。
3. Manager 尝试解析目标镜像的 KMI。解析失败，或使用内置 LKM 而内置列表无法匹配时，会要求手动选择；使用自定义 LKM 时，已识别的 KMI 不必出现在内置列表中。应先确认目标内核，而不是随意选择相近版本。`init_boot` 本身不含内核，因此可能无法单独提供 KMI。
4. 按需设置 SuperKey 等选项，执行修补并保存日志。
5. 文件修补输出到设备 `Download` 目录，以日志中的文件名为准。此步骤生成文件，不自动把选中的本地文件刷入分区。
6. 使用适合设备的刷写工具，将输出写到原镜像对应分区及正确槽位，然后重启。

安装页还可从 HTTPS 地址探测并提取远程固件包中的启动镜像；支持的格式与解析结果以界面为准。远程文件仍需与设备固件匹配。

已经有 root 时，可选择直接安装到当前槽位；A/B 设备还会显示安装到非活动槽位。后者面向已写入更新的目标槽位，必须核对其镜像与 KMI，不能默认沿用当前运行内核的信息。直接安装会写分区，检查执行日志和结果后再重启。

## 直接把 LKM 注入 boot 内核

安装页对本地文件、直接安装和非活动槽位安装提供此选项，对应 `boot-patch-v2`。

| 项目 | 普通 LKM 修补 | 直接 LKM 注入 |
| --- | --- | --- |
| 主要修改内容 | ramdisk 中的 init、模块和配置 | boot 中的 ARM64 内核 Image 与嵌入模块 |
| 目标 | 根据设备选择 `boot` / `init_boot` | 仅含内核的 `boot.img` / `boot` 分区 |
| KMI | Manager 先解析目标，必要时手动选择 | 使用内置 LKM 时从目标内核解析；界面不走普通 KMI 选择 |
| 文件输出参数 | `--out` 为目录 | `--output` / `--out` 为文件 |

该路径依赖目标内核格式、符号与 ABI 检查，并非任意 ARM64 boot 都可修补。它不支持以 `init_boot` 或 `vendor_boot` 代替 boot，也不是 `CONFIG_KSU=y`。Ramdisk 编辑器支持更多镜像类型，不代表这些镜像都能用于此安装方式。

不同安装方式切换时，设备模式可能还会清理同槽位的旧 ramdisk 补丁；应检查日志中实际涉及的分区，并保留其原始镜像。

## 认证与首次启动

- 未设置 SuperKey 时，按管理器签名路径识别。自签 APK 需要对应的 LKM 信任配置，具体见[集成指南](how-to-integrate.md)。
- 设置 SuperKey 后，按安装时配置的认证模式使用该密钥。签名绕过选项切换为仅 SuperKey 认证，不是关闭 Linux 模块签名校验。
- “允许 shell”是允许 Android shell UID 获取 root；启用 adbd 是另一项设置。两者均按需配置。
- 重启后检查首页的内核版本、认证状态、UAPI 匹配情况与 ksud 版本。需要时点击 ksud 卡片同步 APK 内置程序。
- 按需给应用授权，并在该应用中验证 root 访问。SUMHP 已内置，普通模块挂载不要求额外安装一个挂载 MetaModule。

`auto` 后端在适用条件下选择 OverlayFS，其次 Magic Mount；SUMH 需显式选择。OverlayFS / Magic Mount 的变更通常需要重启，热挂载只覆盖符合条件的 SUMH 模块。细节见 [SUMH 验证](sumh-validation.md)。

## 升级与卸载

Manager 只检查公开的 `wuluoy-creator/ZySU` 仓库中的正式 Release，没有 CI 更新通道。APK、ksud 和内核模块分别有版本；更新 APK 后仍需按需要同步 ksud、重新修补启动镜像并重启。OTA 后应重新核对目标固件与 KMI。

已安装的旧 APK 若仍检查 `ZySU-Releases`，请从 [ZySU Releases](https://github.com/wuluoy-creator/ZySU/releases) 手动安装一次包含新更新地址的 APK。覆盖升级需要沿用原签名并使用更高版本码；把旧 APK 复制到新仓库不会更改其内置地址。此后发布统一在 `ZySU`，旧仓库仅保留历史记录和迁移说明，详见[发布与版本管理](../releases.md#从独立发版仓库迁移)。

“恢复启动镜像”与“永久卸载”是不同操作。前者调用 `boot-restore -f`：普通 ramdisk 修补可使用匹配的原始备份或清理补丁，直接注入的恢复也会验证匹配备份。不能假定任意备份都适用。

永久卸载会删除用户空间及模块目录、尝试恢复启动镜像、卸载 Manager，并在约 5 秒后请求重启。当前实现即使恢复失败也会继续后续卸载与重启，因此应在执行前准备好原始镜像和外部恢复工具。单独删除 APK 则不会恢复启动分区。

无法启动时，使用设备可用的恢复工具将匹配固件的原始镜像写回实际修改过的分区及槽位。模块引起的问题可尝试安全模式：内核早期输入 hook 收到至少三次音量下按下事件后会标记安全模式，userspace 会禁用模块并跳过对应启动处理；这依赖 hook 生效时机，不替代原始镜像恢复。

## 排查信息

报告时提供固件、`uname -r`、KMI、三个组件版本、普通修补或直接注入、分区 / 槽位、自定义 LKM 情况，以及修补与启动日志。可在已授权的 root shell 中读取：

```sh
/data/adb/ksud --version
/data/adb/ksud boot-info current-kmi
/data/adb/ksud debug info
/data/adb/ksud sumhp api system
```

`ksud install` 只安装用户空间组件，不能用它代替启动镜像安装。更多命令见 [CLI 参考](../../ksud-cli.md)，状态含义见[环境可观测性](environment-observability.md)。
