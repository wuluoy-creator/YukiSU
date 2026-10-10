# 发布与版本管理

[Release / 发布与版本管理](../../.github/workflows/release.yml) 负责完整构建及本仓库发布。源码、版本标签、正式发布和更新元数据统一维护在公开的 `wuluoy-creator/ZySU`，下载地址为 [ZySU Releases](https://github.com/wuluoy-creator/ZySU/releases)。本页描述仓库中的工作流实现，不列出未经核对的已发布版本或运行结果。

## 构建和发布入口

在本仓库打开 **Actions → Release / 发布与版本管理 → Run workflow**，选择构建分支或标签。

| 输入 | 行为 |
| --- | --- |
| `publish_release` | 默认关闭，只生成 Actions 产物；开启后发布。 |
| `release_tag` | 发布标签，例如 `v1.8.0`；从已有标签运行时可留空。 |
| `prerelease` | 将发布标记为预发布，不进入管理器的正式版提醒渠道。 |
| `manager_version_name` / `manager_version_code` | 覆盖管理器显示名称与 Android 数字版本码。 |
| `ksud_version_name` / `ksud_version_code` | 覆盖内置 ksud 的名称与数字版本。 |
| `kernel_version_name` / `kernel_version_code` | 覆盖模块的基础名称与数字版本，模块完整名称还包含构建提交信息。 |
| `kmi` | 默认 `all`，也可选择一个 KMI；所选范围同时决定 ksud 和 APK 内置的模块。 |

Release 始终请求完整 Manager 构建，包括 ksud、ksuinit 和所选 LKM。仅编译某个组件使用 [Build ZySU / 自选编译](guide/workflow-build.md)。当前 KMI 清单为 `android14-6.1`、`android15-6.6`、`android16-6.12`、`android17-6.18`；它是构建矩阵，不是真机兼容认证。

推送 `v*` 标签会自动发布，无需手动打开 `publish_release`。标签推送时，标签名含 `-` 会自动标记为预发布；手动运行使用 `prerelease` 输入。Release 运行通过同一并发组串行执行。

## 发布仓库和签名

工作流直接发布到运行工作流的公开仓库（`github.repository`），并在预检时检查仓库公开状态。官方源码、构建和发布均使用 `wuluoy-creator/ZySU`。

| 配置 | 用途 |
| --- | --- |
| 内置 `GITHUB_TOKEN` | 查询本仓库 Release；发布任务声明 `contents: write`，用于创建标签和发布附件，无需手动创建令牌。 |
| Secrets `KEYSTORE`、`KEY_ALIAS`、`KEYSTORE_PASSWORD`、`KEY_PASSWORD` | APK 签名密钥库的 Base64 内容、别名和密码；发布要求配置签名。 |
| Secrets `GPG_PRIVATE_KEY`、`GPG_PASSPHRASE` | 可选的 GPG 附加签名。 |

沿用同一 APK 签名密钥才能覆盖升级现有安装。工作流将该证书的公共信息写入本次 LKM，并校验最终 APK 的签名证书，详见 [构建与签名](guide/workflow-build.md)。

只需在本仓库维护签名配置，无需独立发版仓库、发布目标变量或跨仓库令牌。Fork 的工作流发布到该 Fork；若其 Manager 需要独立更新渠道，还需修改应用中的更新地址。

构建前会读取本仓库的 Release 信息并校验版本。即使只是构建产物，版本解析也会查询本仓库，并依赖保留的相应版本标签；仓库不可访问或缺失所需标签会导致计划失败。

## 版本规则

规则实现在 [resolve_versions.py](../../scripts/ci/resolve_versions.py)：

- 名称优先使用显式输入，其次使用本次 Release 标签，否则从 `git describe` 生成；没有语义版本标签时可生成 `0.0.0-<提交哈希>`。管理器名称带 `v` 前缀，ksud 与内核基础名称不带。
- 自定义名称接受 `1.8.0`、`v1.8.0`、`1.8.0-rc.1` 等形式，最多 80 个字符。数字版本范围为 `1..2100000000`。
- 自动数字版本取 `6865 + 当前提交数` 与各已发布 APK 版本码加上对应标签之后提交数增量的最大值，发布时再加 1。草稿不参与计算，预发布参与。
- 发布时显式管理器版本码必须高于已发布版本，并且不低于本次自动值。未发布的手动高版本不参与计算；同一提交反复生成普通 CI 包不保证版本码增加。

保留本仓库中发布对应的源码标签，并让它们指向实际构建提交。工作流用这些标签作为提交计数基点。

Build 手动入口没有内核数字版本输入；需要该输入可使用 Release 入口并保持 `publish_release` 关闭，或使用独立 LKM 工作流。

## 发布过程与产物

工作流拒绝覆盖已有 Release，也拒绝把已有标签用于另一个提交。正式发布过程为：

1. 在本仓库确认或创建指向实际构建提交的标签。
2. 下载本次运行产物，由 [prepare_release.py](../../scripts/ci/prepare_release.py) 检查最终 APK 包名和版本，整理发布附件。
3. 在本仓库使用已确认的标签创建草稿并上传完整附件，再公开 Release；正式版设为 Latest，预发布不设为 Latest。

Release 的标签及自动生成的源码归档对应实际构建提交。创建草稿后的失败可能留下草稿，重新运行前需先处理同名发布状态。

当前整理脚本发布最终 Manager APK、独立 ksud、所选 LKM、它们已有的 GPG 签名，以及 `update.json`。它不发布 Manager 的 Gradle 中间产物、调试映射或独立 ksuinit 附件；ksuinit 已作为依赖嵌入 ksud。

APK 名称为 `ZySU_<版本名称>_<数字版本>-release.apk`。`update.json` 的管理器版本来自最终 APK 的 Manifest，并记录 APK 文件名、SHA-256、大小及 ksud/内核版本输入。

## 管理器更新与本地版本

管理器正式版更新读取 [本仓库的 Release](https://github.com/wuluoy-creator/ZySU/releases)，优先使用合适的 `update.json`，缺失时可回退到兼容 APK 文件名。正式版更新通过浏览器打开下载地址；这条路径不是应用内下载验签安装。ksud 更新则把当前 APK 内置的二进制同步至 `/data/adb/ksud`。

各组件本地构建逻辑识别 `ZYSU_MANAGER_VERSION_NAME`、`ZYSU_MANAGER_VERSION_CODE`、`ZYSU_KSUD_VERSION_NAME`、`ZYSU_KSUD_VERSION_CODE`、`ZYSU_KERNEL_VERSION_NAME`、`ZYSU_KERNEL_VERSION_CODE`。本地版本推导不同于 Actions，不会运行上述发布版本递增算法。直接编译内核时，自定义版本需要 Python 3 校验；通过 Docker 包装脚本构建时还须确认所需环境变量实际传入容器。

## 从独立发版仓库迁移

后续发布只使用 `wuluoy-creator/ZySU`。原 `ZySU-Releases` 仓库保留为只读历史记录和迁移说明，不再承担构建或发布。

2026-10-10 已迁入以下历史版本的发布说明和全部 14 个附件，附件大小与 SHA-256 均与原仓库一致。源码标签指向原始构建提交，Latest 保持为 `v3.3.4`。

| 历史版本 | Manager 版本码 | 源码提交 |
| --- | --- | --- |
| [v3.3.3](https://github.com/wuluoy-creator/ZySU/releases/tag/v3.3.3) | `32612` | `98c675c8201f994b05b0b9e9f8318f8c80245bef` |
| [v3.3.4](https://github.com/wuluoy-creator/ZySU/releases/tag/v3.3.4) | `32619` | `4c23f1b548c98de7bf7e768216e2ec4390caaf0d` |

旧 APK 的更新地址由其构建时的代码决定。若已安装版本仍读取 `ZySU-Releases`，请从本仓库 Releases 手动安装一次包含新更新地址的 APK，并保持原有签名和递增版本码。移动 Release、复制附件或重新下载相同 APK 都不会改变其内置地址；完成这次升级后，后续正式版更新由本仓库提供。

迁移历史 Release 时应保留标签名称、版本码及附件，并在本仓库保留对应的真实源码标签，以维持自动版本递增的基准。迁移附件不等于重新构建 APK。沿用原 APK 签名密钥，保持现有安装的覆盖升级能力。
