# SUMH 回归验证指南

SUMH 是内核中的 VFS 视图引擎；`ksud` 内置的 SUMHP 控制器负责配置、模块挂载规划、规则恢复及诊断接口。管理器通过 JNI 和控制器呈现状态及执行配置操作。本文给出当前仓库已有的回归入口和设备验证范围，不作为某个设备或发行版的通过证明。

## 验证对象

| 层次 | 实现入口 | 重点 |
| --- | --- | --- |
| 内核协议 | [`uapi/sumh.h`](../../../uapi/sumh.h)、[`sumh_ioctl.c`](../../../kernel/sumh/control/sumh_ioctl.c) | 协议、能力查询、参数边界及错误返回 |
| 内核规则和 VFS | [`kernel/sumh`](../../../kernel/sumh) | 规则生命周期、引用回收、目录视图、属性及进程可见性 |
| 用户空间 | [`userspace/ksud/sumhp`](../../../userspace/ksud/sumhp) | ioctl 客户端、配置校验/持久化、恢复、挂载后端、daemon 传输 |
| 管理器 | [`ui/sumh`](../../../manager/app/src/main/java/com/zying/zysu/ui/sumh) | 能力判断、保存/应用结果、状态刷新与错误显示 |

当前 SUMH 协议版本为 1。内核引擎初始化、控制器可用、某项能力受支持及某项视图已启用，必须分别验证。管理器摘要中的“协议 1”不能证明全部功能已经启用。

## 主机回归

以下命令从仓库根目录运行。推荐使用与 CI 一致的 Linux 环境，安装 Python 3、Clang 和支持 C++17 的 G++。

### 内核函数与接口

```sh
python3 scripts/ci/test_sumh_core.py --cc clang
python3 scripts/ci/test_sumh_vfs_helpers.py --cc clang
python3 scripts/ci/test_sumh_mount_views.py --cc clang
python3 scripts/ci/test_sumh_native_namespace.py --cc clang
python3 scripts/ci/test_selinux_hide_status.py --cc clang
python3 scripts/ci/test_sumh_guards.py --cc clang
python3 scripts/ci/test_sumh_kernel_build.py --cc clang
python3 scripts/ci/test_sumh_rules.py --cc clang
```

| 测试脚本 | 覆盖范围 |
| --- | --- |
| `test_sumh_core.py` | 规则回收、引用所有权、setattr 和错误分支 |
| `test_sumh_vfs_helpers.py` | xattr 边界、目录遍历、无处理需求时的快速路径 |
| `test_sumh_mount_views.py` | 保留模块时的 proc 视图策略与过滤函数 |
| `test_sumh_native_namespace.py` | 已撤除的 namespace 文本投影能力及 aggressive 模式拒绝行为 |
| `test_selinux_hide_status.py` | 配置意图与实际 SELinux 运行状态、初始化失败 |
| `test_sumh_guards.py` | 递归保护、哈希冲突、嵌套任务标记和空桶锁获取 |
| `test_sumh_kernel_build.py` | 内核构建字段校验、状态更新和恢复语义 |
| `test_sumh_rules.py` | 规则导出的分配与清理失败分支 |

这些脚本读取生产 C 函数或源文件，以 VFS、RCU、工作队列、锁等测试替身执行关键分支。它们不加载内核模块，也不进行真实并发压力测试。分支内分配或锁计数的改善不能换算为设备延迟提升比例。

### 用户空间与挂载控制

```sh
python3 userspace/ksud/tests/run_sumh_host_tests.py --cxx g++
python3 userspace/ksud/tests/run_sumhp_daemon_host_tests.py --cxx g++
python3 userspace/ksud/tests/run_mount_alias_host_tests.py --cxx g++
python3 userspace/ksud/tests/run_magic_mount_host_tests.py --cxx g++
python3 userspace/ksud/tests/run_overlay_mount_host_tests.py --cxx g++
```

SUMH 用户空间测试覆盖客户端响应长度/文本边界、驱动探测、规则恢复、持久化失败、配置迁移、内核构建设置及 CLI。daemon 测试包含真实 POSIX socket 的分帧、截止时间和重试处理，但不会启动 Android 上的生产 daemon。挂载测试覆盖分区别名、挂载规划以及 Magic Mount、OverlayFS 的相关生产逻辑，不会在宿主机实际修改 Android 挂载树。

自动化入口为 [`.github/workflows/sumh-tests.yml`](../../../.github/workflows/sumh-tests.yml)。工作流存在或曾执行过，不代表当前提交所有目标的测试已通过；验证报告应记录具体提交、命令、编译器和结果。

Windows 下，内核脚本可通过 `--cc` 指定 NDK 的 `clang.exe`，使用同目录 `ld.lld.exe` 构建本机测试。SUMH 用户空间测试可使用 `--zig` 指定 Zig。daemon 测试的 `--portable-only` 仅执行可移植故障注入部分；`--target x86_64-linux-musl --compile-only` 仅交叉编译，不运行 Linux socket 测试。Windows 模拟传输头不能验证 Android ioctl ABI。

## 管理器与完整构建

从 `manager` 目录执行：

```sh
./gradlew :app:assembleDebug :app:testDebugUnitTest :app:lintDebug
```

这验证管理器编译、JNI 构建、现有单元测试及静态检查。完整内核还应按 [构建指南](workflow-build.md) 对发布所需的目标 KMI 编译，不能用提取函数的主机测试替代。是否完成 APK/ksud/LKM 打包也需检查实际产物。

## 设备基线采集

先记录源码提交、设备型号、Android 版本、内核 KMI、管理器/ksud 版本、加载方式、模块列表和文件系统。在已获授权的 root shell 中读取控制器状态：

```sh
/data/adb/ksud sumhp config show
/data/adb/ksud sumhp api system
/data/adb/ksud sumhp api features
/data/adb/ksud sumhp api hooks
/data/adb/ksud sumhp api mounts
/data/adb/ksud sumhp api backends
/data/adb/ksud sumhp sumh list
/data/adb/ksud sumhp hide list
```

配置表示保存的意图；能力、hooks、挂载状态和规则反映不同的运行信息。报告时同时保留这些输出，不要仅截取管理器中的成功提示。

## 设备验证矩阵

| 场景 | 检查内容 |
| --- | --- |
| 挂载后端 | 分别使用 SUMH、OverlayFS、Magic Mount，检查模块优先级、覆盖文件、链接、读写、属性与目录内容；以运行状态确认实际选用后端 |
| 规则并发 | 持续遍历目录时添加、删除和清空测试规则，检查内核日志、引用生命周期及目录位置是否异常 |
| 文件属性 | 只读/可写源挂载、chmod、chown、truncate、已有文件句柄以及目标内核使用的 idmap 接口 |
| xattr | size-only 查询、短缓冲区、大列表、OverlayFS 属性过滤和原生错误返回 |
| 持久化/恢复 | 规则文件缺失、损坏、不可读、恢复正常及控制请求短暂失败后的重试；读取失败不能被当作清空规则 |
| 管理器操作 | 刷新与保存互斥、能力不可用、保存失败、保存成功但应用失败、重试及离开页面后的反馈 |
| 普通应用视图 | 主进程和 isolatedProcess 的模块保留/卸载策略、mountinfo、maps、statfs、namespace 身份及已打开文件句柄 |
| 重启 | post-fs-data 与 boot-completed 恢复、应用冷启动、不同加载方式下的能力变化 |

规则清空及后端变更应在可恢复的测试环境中执行，保留原配置和测试模块。OverlayFS、Magic Mount 的专用场景分别见 [OverlayFS 验证](overlayfs-validation.md) 和 [Magic Mount 验证](magic-mount-validation.md)。

挂载隐藏目前使用 `normal`。内核拒绝旧 `aggressive` 模式；用户空间恢复旧配置时仅对“不支持”错误回退到 normal，管理器保存时会规范该字段。namespace 身份应联合验证 `readlink`、`stat` 和 `fstat`。跨接口限制和启动属性/SELinux 验证见 [应用环境可观察面](environment-observability.md)。

设备测试结果应包含预期、实际值和失败日志。仅主机回归通过时，应明确标注“主机回归通过，完整内核/设备结果未验证”，不要用固定测试数量、旧产物大小或历史测试记录代替当前提交的验证。
