# 用户空间实现与回归验证

本页说明当前 `ksud`、`ksuinit` 和内置 SUMHP 的实现边界，以及如何重复运行仓库提供的回归测试。它不是某一次构建的通过记录；实际结果应附带提交号、工具链、命令和日志。

## 代码职责

| 位置 | 职责 |
| --- | --- |
| [`userspace/ksud/src`](../userspace/ksud/src) | C++ CLI、su、模块管理、镜像处理、文件和子进程操作 |
| [`userspace/ksud/sumhp`](../userspace/ksud/sumhp) | 内置控制器、配置、守护进程、SUMH / OverlayFS / Magic Mount 后端 |
| [`userspace/ksuinit`](../userspace/ksuinit) | ramdisk 安装路径中的早期 init、LKM 加载和系统 init 交接 |
| [`userspace/common`](../userspace/common) | 共用 JSON、su 路径、API 级别与精简 C++ 运行时辅助实现 |
| [`userspace/ksud/third_party`](../userspace/ksud/third_party) | 静态集成的工具及其依赖 |

SUMHP 与 ksud 一起构建，不需要另装挂载控制器模块。SUMH 是内核支持的后端和视图功能；OverlayFS 与 Magic Mount 是另外两种挂载实现。选择挂载后端不等于启用全部隐藏功能，具体关系见 [CLI 参考](ksud-cli.md)。

## 当前实现中的性能与可靠性措施

| 范围 | 实现 | 保留的边界 |
| --- | --- | --- |
| ksuinit 符号解析 | 按模块导入符号流式扫描 kallsyms，避免保留全部符号 | 零地址不能用于解析；仍受内核符号和模块 ABI 约束 |
| LKM ELF 解析 | 检查长度、类型、偏移、表关联和字符串，处理未对齐记录 | 解析成功不代表模块可在任意内核加载 |
| init 交接 | 原子恢复 `/init`，失败时尝试真实 init；非 PID 1 调用拒绝执行启动流程 | 需要设备验证实际根文件系统、SELinux 和 PID 1 行为 |
| 模块管理 | 批量操作合并 init RC 刷新；扫描处理 `DT_UNKNOWN`；等待重试 `EINTR` | 模块脚本本身的行为不由宿主单测保证 |
| 配置和资产写入 | 原子写入、权限处理、输入校验；替换前先暂存 | 不能把失败的写入或部分更新当作成功 |
| 文件与镜像 I/O | 复制前检查同文件/设备，处理短写、中断、关闭失败和 `fsync` | 块设备写入仍是设备操作，不由模拟测试覆盖 |
| 子进程执行 | 管道资源管理、输出限制、等待错误处理、超时进程组终止 | 启动脚本等待期限是另一套机制；到期后脚本继续运行 |
| Magic Mount 遍历 | 可直接绑定时只访问模块相关节点；需要目录骨架时保留原目录内容 | 原目录重建、whiteout、符号链接与回滚仍需处理 |
| OverlayFS 挂载 | 每轮挂载读取一次挂载表索引并复用已选图层 | 不跨挂载调用缓存；仍需重建已有子挂载 |
| SUMHP 传输 | 有界帧、期限、完整消息校验；丢失响应不自动重放操作 | 请求超时不取消服务端已经开始的操作 |

这些措施可减少不必要的分配、扫描与重复工作。本仓库的宿主测试可以验证逻辑和部分操作次数；没有可由本文推出的统一真机耗时、内存或兼容性结论。

## Linux 宿主测试

在仓库根目录运行，需 Python 3 和相应 C/C++ 编译器。测试脚本通常在临时目录编译测试程序，不要求 root 或 Android 设备。

```sh
python3 userspace/ksud/tests/run_host_tests.py --cxx clang++ --sanitize
python3 userspace/ksud/tests/run_module_host_tests.py --cxx clang++
python3 userspace/ksud/tests/run_script_wait_host_tests.py --cxx clang++
python3 userspace/ksud/tests/run_runtime_host_tests.py --cxx clang++
python3 userspace/ksud/tests/run_hide_bootloader_host_tests.py --cxx clang++
python3 userspace/ksuinit/tests/run_host_tests.py --cxx clang++ --sanitize
python3 userspace/ksud/scripts/test_embed_assets.py
```

| 测试入口 | 主要范围 |
| --- | --- |
| `ksud/tests/run_host_tests.py` | ELF/符号、镜像复制、内核版本、su 参数、xperm、BTF |
| `run_module_host_tests.py` | 模块状态变更、批量操作、配置与失败处理 |
| `run_script_wait_host_tests.py` | 启动脚本等待期限、信号屏蔽 |
| `run_runtime_host_tests.py` | 管道故障注入、真实 POSIX 子进程、文件替换及权限 |
| `run_hide_bootloader_host_tests.py` | 启动属性应用时序、读回和重试 |
| `ksuinit/tests/run_host_tests.py` | ELF、符号、vermagic、init 交接和日志 |
| `scripts/test_embed_assets.py` | 嵌入资产生成逻辑 |

挂载与控制器回归：

```sh
python3 userspace/ksud/tests/run_sumh_host_tests.py --cxx g++
python3 userspace/ksud/tests/run_sumhp_daemon_host_tests.py --cxx g++
python3 userspace/ksud/tests/run_mount_alias_host_tests.py --cxx g++
python3 userspace/ksud/tests/run_magic_mount_host_tests.py --cxx g++
python3 userspace/ksud/tests/run_overlay_mount_host_tests.py --cxx g++
```

对应自动化定义是 [`userspace-tests.yml`](../.github/workflows/userspace-tests.yml) 和 [`sumh-tests.yml`](../.github/workflows/sumh-tests.yml)。前者为解析/早期 init 测试启用 ASan/UBSan；后者还运行 `scripts/ci/test_sumh*.py` 等内核辅助逻辑测试。工作流存在不代表当前提交已执行或通过。

## Windows 与交叉编译

支持 Windows 的便携测试提供 `--zig <zig.exe>`，用它替换 `--cxx`；例如：

```powershell
python userspace/ksud/tests/run_host_tests.py --zig C:/tools/zig/zig.exe
python userspace/ksud/tests/run_runtime_host_tests.py --zig C:/tools/zig/zig.exe --portable-only
python userspace/ksud/tests/run_magic_mount_host_tests.py --zig C:/tools/zig/zig.exe
```

将路径换成实际安装位置，先用脚本的 `--help` 检查参数。完整 POSIX 运行时测试可交叉编译：

```powershell
python userspace/ksud/tests/run_runtime_host_tests.py --zig C:/tools/zig/zig.exe --target x86_64-linux-musl --compile-only
```

`--compile-only` 明确跳过执行，不能记录为运行通过。Windows 的便携测试也不等于 Linux `fork`、挂载或 Android 系统调用已验证。Android 成品构建和资产装配见 [构建指南](zh/guide/workflow-build.md)。

## 设备验证记录

宿主回归之后，分别记录以下实际设备结果：

1. 固件版本、内核/KMI、启动镜像来源、ZySU 提交号、安装路径和使用的 LKM。
2. 冷启动、重启、ksuinit 交接及加载失败处理；直接注入路径需独立验证。
3. su 授权、身份切换、交互 shell、`su -c` 和应用配置。
4. 模块安装、禁用、卸载、脚本、配置持久化和重启后的状态。
5. 每种实际使用的后端、子挂载、SELinux 标签、应用命名空间及卸载策略。
6. 镜像修补、原始镜像备份、槽位选择和恢复流程。

应区分“静态检查通过”“宿主测试通过”“Android 编译通过”“设备验证通过”。模拟内核/VFS 边界、交叉编译或构建成功均不能替代最后一项。挂载专项步骤见 [Magic Mount](zh/guide/magic-mount-validation.md) 和 [OverlayFS](zh/guide/overlayfs-validation.md)。
