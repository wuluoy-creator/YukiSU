# ksud / ksuinit 优化与验证

本次改动针对现有启动、模块管理、文件 I/O 和子进程流程。保留正常路径上的 CLI、模块目录、内核 UAPI、vermagic 重试以及系统 init 的参数和环境传递。

## 改动

| 范围 | 改进 | 兼容性处理 |
| --- | --- | --- |
| ksuinit 符号解析 | 只保存模块导入的符号，流式扫描 kallsyms，避免为全部内核符号分配字符串与哈希节点 | 保留符号后缀规范化和最后匹配语义；隐藏的零地址保持未解析 |
| 两个模块加载器 | 检查 ELF 类型、长度、表偏移、链接、字符串及表重叠，使用 memcpy 读取可能未对齐的记录 | 有效模块沿用原加载流程；损坏输入在修改前失败 |
| ksuinit 启动交接 | 原子替换 /init，恢复失败时直接执行真实 init，避免递归执行自身 | 保留 init.real 的相对链接；LKM 加载失败仍继续系统启动；非 PID 1 调用直接报错 |
| ksuinit 资源与日志 | 复用已挂载的 procfs，仅卸载自身创建的挂载；日志保留 errno、处理 EINTR 和截断 | 临时挂载 RAII 不再分配路径容器；保留日志输出目标 |
| ksud 模块批量操作 | 批量禁用、卸载后统一刷新 init RC 与 Zygisk 快照 | 单模块操作仍立即刷新；批量部分失败时为成功项刷新 |
| 模块扫描与安装 | DT_UNKNOWN 时使用 fstatat 回退；安装脚本始终关闭文件；waitpid 重试 EINTR | 继续排除符号链接目录，保留安装输出和退出状态 |
| 模块配置 | 原子写入，已有文件保留权限位，新文件明确使用 0644；检查模块 ID、键和值 | 原有单行配置格式不变；拒绝路径穿越和不能表示为单行配置的输入 |
| ksud 文件操作 | 复制前检查文件/设备是否相同，防止先截断源文件；二进制写入处理短写、中断和关闭失败 | 普通镜像仍截断到新长度，块设备不截断，写盘成功仍要求 fsync |
| ksud 进程执行 | 共用管道 RAII，处理标准描述符关闭、创建失败和等待错误；超时终止对应进程组 | 保留输出捕获格式与限制；普通命令调用方式不变 |

新增测试通过故障注入检查失败路径，不访问真实模块目录或分区。性能收益来自减少保存的符号和重复刷新次数；尚无真机耗时或内存基准数据。

## 本地验证结果

验证环境：Windows、Android NDK 30.0.16248370、CMake 3.22.1、Zig 0.14.1。

- ksud 与 ksuinit：Android arm64-v8a / API 31 / Release 编译及链接通过。
- 修改的 ksud C++ 源文件通过仓库 clang-tidy；ksuinit 启用 clang-tidy 的完整构建通过。
- ksud：ELF32/ELF64、镜像复制故障注入、模块管理及配置、管道故障注入通过。
- ksuinit：ELF/符号/vermagic（含 10,000 次确定性损坏输入变异）、真实 main 的交接模拟、日志测试通过。
- 已有 su 参数、内核版本、xperm、BTF、Kasumi 客户端/恢复/持久化和 3 个资产打包测试通过。
- POSIX 进程/文件集成测试已交叉编译及链接为 Linux x86_64，Windows 本地未执行。覆盖真实 fork/pipe、超时子进程、原子文件替换、权限与失败保留。
- 新增 `.github/workflows/userspace-tests.yml` 执行 Linux 测试，并为解析测试启用 ASan/UBSan。工作流已配置，本次未提交或触发远程 CI。

本地二进制用于编译验证；未装配完整发布资产或刷入设备。没有连接 Android 设备，尚未验证真实 PID 1 启动、LKM 插入、SELinux/挂载行为和各机型兼容性，因此不把宿主测试视为真机无回归保证。

## 复验命令

在仓库根目录的 Linux 环境执行：

```sh
python3 userspace/ksud/tests/run_host_tests.py --cxx clang++ --sanitize
python3 userspace/ksud/tests/run_module_host_tests.py --cxx clang++
python3 userspace/ksud/tests/run_runtime_host_tests.py --cxx clang++
python3 userspace/ksuinit/tests/run_host_tests.py --cxx clang++ --sanitize
python3 userspace/ksud/tests/run_kasumi_host_tests.py --cxx clang++
python3 userspace/ksud/scripts/test_embed_assets.py
```

Windows 的便携测试以 `--zig <zig.exe 路径>` 替换 `--cxx clang++`，不传 `--sanitize`；运行时测试另加 `--portable-only`。完整 POSIX 测试可用 `--target x86_64-linux-musl --compile-only` 交叉编译，但这不代表已执行。

发布前应在支持的设备上复验冷启动、LKM 失败继续启动、su 命令、模块安装/启停/批量操作、配置持久化以及启动镜像处理。沿用项目构建脚本装配对应内核模块与嵌入资产。
