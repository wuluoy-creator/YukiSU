# GitHub Actions 与本地构建

源码、构建定义和 [ZySU Releases](https://github.com/wuluoy-creator/ZySU/releases) 统一位于公开的 `wuluoy-creator/ZySU` 仓库。运行官方 Actions 需要仓库写入权限；也可 Fork 后在自己的仓库配置构建。某次运行是否成功，以对应运行记录为准。

## 选择组件

打开 **Actions → Build ZySU / 自选编译 → Run workflow**，选择分支或标签、组件与 KMI。默认选择 Manager，`kmi=all`。[构建计划脚本](../../../scripts/ci/plan_build.py) 自动补齐依赖：

| 选择 | 自动添加的依赖 | 下载产物 |
| --- | --- | --- |
| Manager APK | ksud、所选 KMI 的 LKM、ksuinit | `Manager-arm64-v8a` |
| LKM | 无 | `<KMI>-lkm` |
| ksud | 所选 KMI 的 LKM、ksuinit | `ksud-aarch64-linux-android` |
| ksuinit | 无 | `ksuinit-aarch64-linux-android` |

单独编译组件时先取消 Manager；全部取消会在计划阶段报错。

`all` 包括 `android14-6.1`、`android15-6.6`、`android16-6.12`、`android17-6.18`。选择单个 KMI 后，ksud 和 Manager 只内置该 KMI 的模块。通过 `workflow_call` 调用时也可传入逗号分隔列表，非法目标会被拒绝，重复项会被去重。单独编译 ksuinit 时，KMI 不影响该组件。

这是 ARM64 构建矩阵，不是已验证设备列表。内核最低要求 Linux 6.1，实际兼容性仍取决于设备内核，详见 [内核模块编译](how-to-integrate.md)。

## 下载最终安装包

运行页面的 Summary 显示编译计划、任务结果和产物链接。安装管理器应下载 **`Manager-arm64-v8a`**。`Manager-gradle-arm64-v8a` 是重新嵌入本次构建的 ksud 并签名前的中间 APK；`Manager-mappings-arm64-v8a` 是调试映射。

构建链为 LKM + ksuinit → ksud → 最终 APK。Manager 的 Gradle 编译与原生任务可并行，重新打包等待依赖完成。文档列出任务或产物名称不表示它已成功构建；应检查具体运行记录，再在目标设备验证。

## APK 签名与管理器认证

为保持 APK 更新签名一致，在源码仓库配置以下 Secrets：

| Secret | 内容 |
| --- | --- |
| `KEYSTORE` | JKS 密钥库的 Base64 内容 |
| `KEY_ALIAS` | 密钥别名 |
| `KEYSTORE_PASSWORD` | 密钥库密码 |
| `KEY_PASSWORD` | 密钥密码 |

LKM 工作流从同一密钥库导出公共证书的大小和 SHA-256，由 Kbuild 嵌入管理器身份；重新打包步骤校验最终 APK 证书是否匹配。传给内核构建的是公共证书信息。

未配置密钥库时，[签名 Action](../../../.github/actions/setup-manager-signing/action.yml) 生成临时密钥，不同运行的 APK 无法保持相同的覆盖安装身份。模块仍使用默认信任证书，临时签名管理器需要在修补镜像时配置 SuperKey 认证，或另行配置动态管理器信任。只更新 APK 不会替换内核模块内的信任证书。

`GPG_PRIVATE_KEY` 与 `GPG_PASSPHRASE` 用于可选的 GPG 附加签名，与 Android APK 签名分开。LKM、ksud、ksuinit 没有 `.sig` 时仍可上传；APK 的 GPG 签名还要求配置 APK 密钥库。

## 工作流入口

| 工作流 | 触发方式和用途 |
| --- | --- |
| [Build ZySU / 自选编译](../../../.github/workflows/build.yml) | 手动选择组件或复用调用，生成产物。 |
| [Build Manager](../../../.github/workflows/build-manager.yml) | `main`、`dev`、`feat/*` 推送及目标为 `main`/`dev` 的 PR 命中路径过滤时触发，也可复用调用，执行完整构建。 |
| [Build LKM for KernelSU](../../../.github/workflows/build-lkm.yml) | 手动或复用的独立 LKM 构建，支持内核版本覆盖。 |
| [Release / 发布与版本管理](../../../.github/workflows/release.yml) | 手动构建/发布，或推送 `v*` 标签时自动发布。 |

仅修改文档不一定触发完整构建。同一并发组中新的自动分支/PR 构建可取消旧运行；手动构建独立，Release 运行串行排队。

## 发布与版本

Build 手动入口提供管理器和 ksud 的名称、数字版本，以及内核名称；Release 还提供内核数字版本、发布标签、发布/预发布开关。留空时，[resolve_versions.py](../../../scripts/ci/resolve_versions.py) 根据 Release 标签或 Git 生成名称，根据 Git 历史和已发布 APK 版本码生成数字版本。

Release 在运行工作流的公开仓库（`github.repository`）中直接发布，官方目标为 `wuluoy-creator/ZySU`。发布任务使用内置 `GITHUB_TOKEN` 和 `contents: write` 权限，版本查询使用同一仓库的 Release 信息；只需维护上述 APK 签名配置，无需独立发版仓库或额外的跨仓库令牌。Fork 的 Release 工作流发布到该 Fork，自行编译的 Manager 若要使用独立更新渠道，还需修改其更新地址。

手动 Release 默认只生成产物；勾选 `publish_release` 并填写标签后发布。推送 `v*` 标签则自动发布。工作流在本仓库保留指向实际构建提交的标签，创建 Release 草稿、上传完整附件后再公开，不覆盖已有 Release。详细规则和旧 APK 的迁移方式见 [发布与版本管理](../releases.md)。

## 本地构建入口

[build.sh](../../../scripts/build.sh) 与 [build.bat](../../../scripts/build.bat) 构建完整 ARM64 包。默认编译全部 4 个 KMI；`-k` 选择一个；`--skip-lkm` 复用匹配的 `out/<KMI>_kernelsu.ko`；`--clean` 清理原生 CMake 构建目录；`-i` 请求通过 adb 安装 APK。

Linux 查看 `bash scripts/build.sh --help`，Windows 查看 `.\scripts\build.bat --help`。本地需要 Android SDK/NDK、JDK、CMake、Ninja、Python 3，以及 DDK 阶段使用的 Docker。Windows 脚本使用 Docker 编译 LKM，使用 Windows 工具编译用户空间与管理器，并从 [版本目录](../../../manager/gradle/libs.versions.toml) 读取所需 NDK。检出时需保留 Git 符号链接，见 [模块编译说明](how-to-integrate.md)。

本地签名变量为 `ZYSU_KEYSTORE`、`ZYSU_KEYSTORE_PASSWORD`、`ZYSU_KEY_ALIAS`、`ZYSU_KEY_PASSWORD`。未配置签名时 release APK 未签名。本地脚本不会像 Actions 那样自动导出并传入自定义管理器证书信息，测试本地包时仍需配置对应管理器认证方式。

当前 Manager 的工具链配置为 JDK 21、Android SDK 37、Build Tools 36.1.0、NDK 30.0.16248370 和 CMake 3.22.1，原生 userspace 以 API 31 为最低目标。先初始化子模块，并让 Docker 能运行 `linux/amd64` 容器及拉取 `ghcr.io/ylarod/ddk-min:<KMI>-20260828`。

Linux / macOS 的 shell 脚本要求设置 `ANDROID_NDK_HOME`；同时为 Gradle 配置 `JAVA_HOME` 和 Android SDK 路径，并确保 `cmake`、`ninja`、`python3`、`docker` 可调用。Windows 可用 `ANDROID_SDK_ROOT` / `ANDROID_HOME`、`ANDROID_NDK_HOME`、`JAVA_HOME`、`PYTHON` 指定工具；脚本从 SDK 目录寻找 CMake / Ninja，不会自动下载缺失的 SDK 包。

从仓库根目录运行 `bash scripts/build.sh --kmi android14-6.1`，Windows 使用 `.\scripts\build.bat --kmi android14-6.1`。默认复用原生构建缓存；更换工具链后可使用 `--clean`。脚本会重新整理 `userspace/ksud/assets/` 中的生成资产，并把 ksud 复制为 Manager 的 `libksud.so`。

| 本地产物 | 路径 |
| --- | --- |
| LKM | `out/<KMI>_kernelsu.ko` |
| ksuinit | `userspace/ksuinit/build/ksuinit` |
| ksud | `userspace/ksud/build/ksud` |
| 最终 APK | `manager/app/build/outputs/renamed_apk/release/` |

签名后的 APK 才能安装；`-i` 只调用 adb 安装 APK，不会把 LKM 刷入启动镜像。安装后按[安装指南](installation.md)同步 ksud，并按需修补目标镜像。
