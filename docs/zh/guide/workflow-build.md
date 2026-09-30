# 使用 GitHub Actions 编译

在自己的 Fork 中打开 **Actions → Build YukiSU / 自选编译 → Run workflow**，选择分支和需要的组件，再点击运行。首次使用 Fork 时，请先在 Actions 页面启用工作流。

默认只勾选 **Manager APK**，`kmi` 为 `all`，并启用 `enable_yukizygisk`；这会生成内置全部受支持内核模块及 YukiZygisk 的完整管理器，无需逐项勾选依赖。

## 选择编译内容

想单独编译组件时，先取消默认的 **Manager APK**，再勾选所需组件，支持多选。工作流会自动补齐依赖，每个组件只编译一次；全部取消会在开始编译前报错。

| 选择 | 自动编译的依赖 | 下载产物 |
| --- | --- | --- |
| Manager APK | ksud、所选 KMI 的 LKM、ksuinit；启用内置支持时加上 YukiZygisk | `Manager-arm64-v8a` |
| LKM | 无 | `<KMI>-lkm` |
| ksud | 所选 KMI 的 LKM、ksuinit；启用内置支持时加上 YukiZygisk | `ksud-aarch64-linux-android` |
| ksuinit | 无 | `ksuinit-aarch64-linux-android` |
| YukiZygisk 独立组件 | 无其他可选组件；包含 64 位和 32 位载荷 | `yukizygisk-aarch64-linux-android` |

`enable_yukizygisk` 控制 Manager/ksud 是否内置 YukiZygisk，以及所编译 LKM 是否启用对应内核钩子。它本身不是一个编译组件：只选 LKM 时，开启它也不会额外编译独立载荷。

**YukiZygisk 独立组件**（`build_yukizygisk`）则要求生成载荷产物，即使关闭 `enable_yukizygisk` 也会编译，但不会因此把载荷嵌入 Manager/ksud，也不会打开 LKM 钩子。

## 选择内核版本

`kmi` 默认 `all`，编译全部 4 个受支持版本：`android14-6.1`、`android15-6.6`、`android16-6.12` 和 `android17-6.18`，适合制作通用包。最低支持 Linux 6.1。也可以从下拉框选择设备对应的版本，例如 `android15-6.6`，减少编译任务。

选择单个 KMI 时，生成的 **Manager APK 和 ksud 也只内置该版本的内核模块**，不包含其他版本；需要通用包时请保持 `all`。只编译 ksuinit 或 YukiZygisk 时，KMI 不影响这两个组件。

其他工作流通过 `workflow_call` 调用此入口时，还可以传入逗号分隔的 KMI 列表，例如 `android14-6.1,android15-6.6`。只接受受支持的版本，重复项会去重。

## 下载与签名

运行页面的 Summary 会显示所选组件、自动补齐的依赖及任务结果。完成后，在页面下方的 **Artifacts** 下载对应产物。

安装管理器请下载 **`Manager-arm64-v8a`** 并解压取得 APK。`Manager-gradle-arm64-v8a` 是尚未嵌入 ksud 的 Gradle 中间产物，不是最终安装包；`Manager-mappings-arm64-v8a` 用于调试。

无需配置签名密钥即可进行手动编译。未配置正式 APK 密钥时，工作流为每次运行生成一份临时签名，**不同运行的 APK 不能直接互相覆盖安装**。要保持更新签名一致，在仓库 **Settings → Secrets and variables → Actions → Repository secrets** 配置以下正式签名信息：

| Secret | 内容 |
| --- | --- |
| `KEYSTORE` | JKS 密钥库文件的 Base64 内容 |
| `KEY_ALIAS` | 密钥别名 |
| `KEYSTORE_PASSWORD` | 密钥库密码 |
| `KEY_PASSWORD` | 密钥密码 |

GPG 附加签名与 Android APK 签名不同。未配置 `GPG_PRIVATE_KEY` 时，LKM、ksud 和 ksuinit 仍会上传，不生成 `.sig` 文件；配置时使用 `GPG_PRIVATE_KEY` 和 `GPG_PASSPHRASE`，普通编译可以使用自己的 GPG 密钥。Manager APK 同时配置正式 APK 密钥后才生成 GPG 附加签名；官方 nightly 保留指定的受信任签名身份。

手动运行只生成 Actions 产物，不更新 nightly 发布渠道。自动完整编译仍使用 **Build Manager** 工作流，保留现有更新检查和 nightly 下载入口。

## 编译效率

工作流只构建所选组件及其依赖；不同 KMI、独立组件与 Manager 的 Gradle 编译可并行执行，原生编译使用多核。Gradle 构建缓存和 userspace 的 ccache 可复用之前的编译结果，下载产物时也只获取所需文件。

同一分支或 PR 的新自动编译会取消旧的未完成任务，避免重复占用运行资源。手动运行和标签发布保持独立，不会因另一次手动编译而被取消。实际耗时取决于所选组件、KMI 数量、缓存命中及 GitHub runner 排队情况。
