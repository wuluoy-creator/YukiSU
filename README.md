# ZySU

<img align="right" src="docs/ZySU-mini.png" width="180" alt="ZySU 标志">

ZySU 是从 [SukiSU-Ultra](https://github.com/ShirkNeko/SukiSU-Ultra) 分叉、基于 KernelSU 的 Android root 方案。此公开仓库统一维护源码、构建工作流和正式发布，包含 ARM64 内核模块、C++17 用户空间组件和 Kotlin / Jetpack Compose 管理器。

[English](docs/README.md) · [中文文档](docs/zh/README.md) · [日本語](docs/ja/README.md) · [Türkçe](docs/tr/README.md) · [Русский](docs/ru/README.md)

[下载发布版](https://github.com/wuluoy-creator/ZySU/releases) · [安装指南](docs/zh/guide/installation.md) · [自选编译](docs/zh/guide/workflow-build.md) · [问题反馈](https://github.com/wuluoy-creator/ZySU/issues)

## 当前功能

- **Root 与授权**：内核侧 `su`、按应用授权和 App Profile、管理器签名识别、动态管理器与 SuperKey 认证。
- **模块管理**：安装、启停、卸载、启动脚本、模块 WebUI、仓库与更新管理；保留外部 MetaModule 生命周期接口。
- **内置挂载控制器 SUMHP**：提供 OverlayFS、Magic Mount、SUMH 和不挂载模式，可设置全局后端或逐模块规则。`auto` 优先选择适用的 OverlayFS，再尝试 Magic Mount；SUMH 需要显式选择。
- **SUMH 与环境控制**：内核路径规则、挂载视图处理、运行状态查询，以及管理器中的配置和诊断。功能是否可用以设备运行时探测为准。
- **启动镜像与分区工具**：普通 LKM 镜像修补、直接 LKM 镜像注入、备份与恢复、分区操作、AnyKernel3 刷写和 ramdisk 编辑。
- **运行时设置**：ADB root、su 日志、SELinux 隐藏状态、bootloader 属性处理、自定义 su 路径和模块 `init.rc` 注入。

这些功能不代表所有第三方模块、厂商内核或环境检测程序都已通过兼容性验证。

## 支持范围

| 项目 | 当前范围 |
| --- | --- |
| 设备架构 | ARM64；Manager 和 Android userspace 仅构建 `arm64-v8a` |
| Manager / userspace 最低版本 | Android 12 / API 31 |
| 内核 | Linux 6.1 及以上，现成构建面向下面列出的 Android GKI KMI |
| 内核构建形式 | `CONFIG_KSU=m`；不支持 `CONFIG_KSU=y` |
| 默认 LKM 构建目标 | `android14-6.1`、`android15-6.6`、`android16-6.12`、`android17-6.18` |

Android 系统版本、Linux 版本和 KMI 是不同条件。Manager 能安装，不代表设备能加载对应内核模块；四个 KMI 是构建矩阵，不是真机认证列表。旧内核、其他 KMI 和 non-GKI 设备不在现成产物的支持范围内。

“把 LKM 嵌入 boot 内核”的安装选项仍使用模块产物，不表示恢复了 `CONFIG_KSU=y` 支持。

## 开始使用

1. 从 [ZySU Releases](https://github.com/wuluoy-creator/ZySU/releases) 获取 Manager APK，确认架构、内核和 KMI。
2. 阅读[安装指南](docs/zh/guide/installation.md)，准备与设备当前固件匹配的原始启动镜像及恢复方式。
3. 在 Manager 安装页选择镜像修补；已有 root 时也可使用直接安装。按照实际分区和槽位部署生成的镜像。
4. 重启后确认管理器能识别内核、完成认证，并检查已安装 ksud 与应用内置版本。再配置授权和模块。

Manager 从本仓库的 [Releases](https://github.com/wuluoy-creator/ZySU/releases) 检查正式版更新；Actions 测试产物需自行下载，没有 CI 更新通道。单独更新 APK 不等同于更新启动镜像中的内核模块。

旧 APK 若仍读取 `ZySU-Releases`，需手动安装一次包含新更新地址的 APK；迁移已有发布附件不会改写 APK 内置地址。旧发布仓库仅保留历史记录和迁移说明，后续版本统一在本仓库发布，详见[发布与迁移说明](docs/zh/releases.md)。

## 从源码构建

```sh
git clone --recurse-submodules https://github.com/wuluoy-creator/ZySU.git
cd ZySU
```

已有检出可运行 `git submodule update --init --recursive`。需要保留 Git 符号链接，特别是 `kernel/include/uapi`；Windows 构建前请检查检出方式。

完整产物的构建入口是 **Actions → Build ZySU / 自选编译**：选择 Manager 会自动构建 LKM、ksuinit 和 ksud，再把 ksud 打入 APK。下载最终的 `Manager-arm64-v8a`，不要把中间的 `Manager-gradle-arm64-v8a` 当作完整安装包。

本地入口如下，均从仓库根目录运行：

```sh
# Linux / macOS shell：只打包指定 KMI
bash scripts/build.sh --kmi android14-6.1
```

```powershell
# Windows：原生 PowerShell 构建入口
.\scripts\build.bat --kmi android14-6.1
```

不传 `--kmi` 时构建全部四个目标。构建需要 Docker DDK、Python 3、CMake / Ninja、Android SDK / NDK 与 JDK 21；当前 Manager 使用 SDK 37、Build Tools 36.1.0、NDK 30.0.16248370。完整环境、签名配置、产物位置及本地脚本与 CI 的差异见[编译指南](docs/zh/guide/workflow-build.md)和[LKM 集成指南](docs/zh/guide/how-to-integrate.md)。

## 仓库结构

| 路径 | 用途 |
| --- | --- |
| `kernel/` | root、授权、hook、SUMH、SELinux 和运行时内核实现 |
| `uapi/` | 内核与 userspace / Manager 共用接口定义 |
| `userspace/ksud/` | C++ 守护程序、su、模块管理、镜像和分区工具、内置 SUMHP |
| `userspace/ksuinit/` | 启动早期加载模块并移交给原始 init |
| `userspace/common/` | 原生组件共用的辅助代码 |
| `manager/` | Android Manager、JNI、单元测试和资源 |
| `js/` | 兼容 KernelSU 模块 WebUI 的 JavaScript API |
| `scripts/`、`.github/` | 本地构建、构建规划、版本与发布处理、CI 检查 |
| `docs/` | 安装、构建、CLI、协议与验证说明 |

## 开发与验证

主机回归入口位于 `scripts/ci/test_*.py`、`userspace/ksud/tests/` 和 `userspace/ksuinit/tests/`；Manager 测试位于 `manager/app/src/test/`。所需编译器与执行命令见[验证文档索引](docs/zh/README.md)。主机测试和源码检查不能替代真机启动、模块挂载及恢复验证。

提交问题时请提供：设备与固件版本、`uname -r`、KMI、Manager / ksud / 内核版本、安装方式、是否使用自定义 LKM、复现步骤和相关日志。请在日志中移除密钥和私人数据。安全问题按 [SECURITY.md](SECURITY.md) 处理。

## 许可证与来源

内核部分参见 [kernel/LICENSE](kernel/LICENSE)（GPL v2），其他项目代码参见根目录 [LICENSE](LICENSE)（GPL v3）；具体文件头与第三方组件自带许可证优先适用。`js/package.json` 声明 WebUI 库使用 Apache-2.0，不能把所有第三方代码统一描述成 GPL。

感谢 [KernelSU](https://github.com/tiann/KernelSU)、[SukiSU-Ultra](https://github.com/ShirkNeko/SukiSU-Ultra)、[Magisk](https://github.com/topjohnwu/Magisk) 及仓库所使用的第三方项目。第三方原生组件说明见 [userspace/ksud/third_party/README.md](userspace/ksud/third_party/README.md)。
