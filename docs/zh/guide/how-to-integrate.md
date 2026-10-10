# 编译内核模块

ZySU 以 ARM64 可加载内核模块 `kernelsu.ko` 运行，支持的配置为 `CONFIG_KSU=m`，没有受支持的 `CONFIG_KSU=y` 内置集成流程。[Kbuild](../../../kernel/Kbuild) 会拒绝 Linux 6.1 以下的内核和非 ARM64 目标。

## 构建目标

DDK 工作流与本地构建脚本使用以下矩阵：

| KMI 目标 | 内核系列 |
| --- | --- |
| `android14-6.1` | 6.1 |
| `android15-6.6` | 6.6 |
| `android16-6.12` | 6.12 |
| `android17-6.18` | 6.18 |

这是构建目标清单，不是真机验证清单。内核版本相同不代表模块一定兼容，还取决于 KMI、配置、符号和模块加载策略。打包构建见 [Actions 编译](workflow-build.md)，部署见 [安装指南](installation.md)。

## 使用设备对应的内核构建树

准备 Linux、Make、Clang/LLVM、Git，以及目标内核要求的工具链。先按设备配置完成内核构建，保留生成的头文件、符号信息和未剥离的 `vmlinux`；仅下载源码或运行 `modules_prepare` 不足以完成本仓库的构建及符号检查。

保留完整的 ZySU 仓库。`kernel/include/uapi` 是指向 `../../uapi` 的 Git 符号链接。Windows Git 未启用符号链接时可能将其检出为普通文本，应在 Linux/WSL 构建环境中使用保留真实符号链接的检出。

在仓库根目录运行下面的命令，先将示例路径替换为真实绝对路径：

```sh
export KDIR="/absolute/path/to/kernel/out"
export CLANG_PATH="/absolute/path/to/clang/bin"
export PATH="$CLANG_PATH:$PATH"
export ARCH=arm64
export LLVM=1
export LLVM_IAS=1
export CROSS_COMPILE=aarch64-linux-gnu-

test -f "$KDIR/vmlinux"
test -f kernel/include/uapi/supercall.h
make -C kernel CONFIG_KSU=m CONFIG_KSU_SUPERKEY=y CC=clang
llvm-strip -d kernel/kernelsu.ko
```

`KDIR` 指向已配置的内核构建目录；使用 `O=out` 时通常指向输出目录。[Makefile](../../../kernel/Makefile) 调用外部模块构建流程，再用 `check_symbol` 对照 `vmlinux` 检查符号。输出为 `kernel/kernelsu.ko`；检查通过不等于设备加载、启动及运行时验证通过。

TSR 系统调用路径使用 `CONFIG_HAVE_SYSCALL_TRACEPOINTS`。自定义内核还需对照 Hook 实现核对模块、跟踪与符号配置。`CONFIG_KRETPROBES` 启用 tracepoint marker 的 kretprobe 路径，该代码也保留未启用时的回退路径。可选的 `CONFIG_KSU_KRETPROBES_SUCOMPAT` 标记为实验功能并依赖 `KRETPROBES`。仓库没有为任意厂商内核提供通用 defconfig。

## 选项与管理器身份

选项定义见 [Kconfig](../../../kernel/Kconfig)。示例显式启用 SuperKey。`CONFIG_KSU_DISABLE_MANAGER=y` 会移除管理器集成和 SuperKey，`CONFIG_KSU_DISABLE_POLICY=y` 会移除应用独立配置。

单独编译模块时，给 APK 换签名不会自动改变模块信任的管理器证书。Kbuild 接受成对的 `KSU_MANAGER_CERT_SIZE` 与 `KSU_MANAGER_CERT_SHA256` 公共证书信息；Actions 会从签名密钥库导出它们，详见 [工作流签名说明](workflow-build.md)。

自定义内核版本使用 `ZYSU_KERNEL_VERSION_NAME` 和 `ZYSU_KERNEL_VERSION_CODE`，需要 Python 3 校验。未覆盖时，Kbuild 根据 Git 推导版本并可能访问 GitHub。

完整本地 APK 构建入口为 [build.sh](../../../scripts/build.sh) 和 [build.bat](../../../scripts/build.bat)（调用 [build.ps1](../../../scripts/build.ps1)）。它们依次构建 DDK 模块、ksuinit、ksud 和管理器，与本页仅针对设备内核编译模块的流程不同。
