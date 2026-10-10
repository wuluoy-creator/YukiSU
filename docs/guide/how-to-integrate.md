# Building the kernel module

ZySU builds a loadable ARM64 kernel module named `kernelsu.ko`. The supported configuration is `CONFIG_KSU=m`; built-in `CONFIG_KSU=y` is not a supported integration path. [Kbuild](../../kernel/Kbuild) rejects kernels older than Linux 6.1 and non-ARM64 targets.

## Build targets

DDK workflows and local build scripts use this matrix:

| KMI target | Kernel series |
| --- | --- |
| `android14-6.1` | 6.1 |
| `android15-6.6` | 6.6 |
| `android16-6.12` | 6.12 |
| `android17-6.18` | 6.18 |

These are build targets, not verified devices. Matching a kernel version alone does not establish compatibility: KMI, configuration, symbols and module loading policy also matter. See [Actions builds](workflow-build.md) and [installation](installation.md).

## Build against your device's kernel

Use Linux, Make, Clang/LLVM, Git and the toolchain required by the target kernel. Configure and build the device's kernel first, retaining generated headers, symbol information and unstripped `vmlinux`. Source code or `modules_prepare` alone is insufficient for this build and its symbol check.

Keep the complete ZySU checkout. `kernel/include/uapi` is a Git symbolic link to `../../uapi`. Windows Git may check it out as a text file when symbolic links are disabled; use a checkout with real links in the Linux/WSL build environment.

Run from the repository root after replacing the example paths:

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

`KDIR` points to the configured kernel build directory, normally the output directory when using `O=out`. [Makefile](../../kernel/Makefile) builds the external module and runs `check_symbol` against `vmlinux`. The output is `kernel/kernelsu.ko`; passing the check does not validate device loading, booting or runtime behavior.

The TSR syscall path uses `CONFIG_HAVE_SYSCALL_TRACEPOINTS`. Check the target's module, tracing and symbol configuration against the hook implementation. `CONFIG_KRETPROBES` enables the tracepoint marker's kretprobe path; a fallback also exists when disabled. The experimental `CONFIG_KSU_KRETPROBES_SUCOMPAT` option depends on `KRETPROBES`. There is no universal vendor-kernel defconfig in this repository.

## Options and Manager identity

[Kconfig](../../kernel/Kconfig) defines the options. The example explicitly enables SuperKey. `CONFIG_KSU_DISABLE_MANAGER=y` removes Manager integration and SuperKey; `CONFIG_KSU_DISABLE_POLICY=y` removes per-app profile customization.

A separately built module does not automatically trust a custom APK signing key. Kbuild accepts the public certificate's `KSU_MANAGER_CERT_SIZE` and `KSU_MANAGER_CERT_SHA256` together. Actions derives these from its configured keystore; see [signing](workflow-build.md).

Kernel version overrides use `ZYSU_KERNEL_VERSION_NAME` and `ZYSU_KERNEL_VERSION_CODE`, validated with Python 3. Without overrides, Kbuild uses Git and may query GitHub.

For a complete local APK build, [build.sh](../../scripts/build.sh) and [build.bat](../../scripts/build.bat), which invokes [build.ps1](../../scripts/build.ps1), build DDK modules, ksuinit, ksud and Manager in sequence. This differs from building only a module against your own kernel tree.
