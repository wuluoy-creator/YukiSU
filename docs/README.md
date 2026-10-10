# ZySU

<img align="right" src="ZySU-mini.png" width="180" alt="ZySU logo">

ZySU is an Android root solution derived from [SukiSU-Ultra](https://github.com/ShirkNeko/SukiSU-Ultra) and KernelSU. This public repository hosts the source, build workflows, and releases together. It contains an ARM64 kernel module, C++17 userspace components, and a Kotlin / Jetpack Compose Manager.

[Repository overview (中文)](../README.md) · **English** · [简体中文](zh/README.md) · [日本語](ja/README.md) · [Türkçe](tr/README.md) · [Русский](ru/README.md)

[Downloads](https://github.com/wuluoy-creator/ZySU/releases) · [Installation](guide/installation.md) · [Build guide](guide/workflow-build.md) · [Issues](https://github.com/wuluoy-creator/ZySU/issues)

## What is implemented

- Kernel `su`, per-app authorization and profiles, Manager signature verification, dynamic Managers, and SuperKey authentication.
- Module installation, enable/disable/removal, boot scripts, module WebUI, and repository management.
- Built-in **SUMHP** mount control with OverlayFS, Magic Mount, SUMH, and no-mount modes. `auto` tries suitable OverlayFS mounting before Magic Mount; SUMH requires an explicit selection. External MetaModule lifecycle interfaces are retained.
- SUMH path and mount-view controls, runtime status, and diagnostics. Availability depends on the running kernel and device.
- Boot image patching and restoration, direct LKM image injection, partition tools, AnyKernel3 flashing, and ramdisk editing.
- ADB root, su logging, SELinux hiding status, bootloader property handling, custom su paths, and module `init.rc` injection.

Implemented features do not establish compatibility with every vendor kernel, third-party module, or environment checker.

## Compatibility

| Component | Current constraint |
| --- | --- |
| Architecture | ARM64; Android artifacts use `arm64-v8a` |
| Manager / userspace | Android 12 / API 31 or later |
| Kernel | Linux 6.1 or later; packaged modules target the GKI KMIs below |
| Kernel build mode | `CONFIG_KSU=m`; built-in `CONFIG_KSU=y` is unsupported |
| Default KMI matrix | `android14-6.1`, `android15-6.6`, `android16-6.12`, `android17-6.18` |

The Android release, Linux version, and KMI are separate requirements. Installing the Manager APK does not establish kernel support. This is a build matrix, not a list of certified devices; older kernels, other KMIs, and non-GKI devices are outside the packaged artifact scope.

Direct LKM injection into a boot kernel still uses a module artifact. It does not provide built-in kernel support.

## Getting started

Read the [installation guide](guide/installation.md) before modifying a boot image. Use an original image matching the device firmware and keep a recovery copy. After rebooting, verify Manager authentication, the kernel version, and the installed ksud before configuring modules.

Official updates come from [ZySU Releases](https://github.com/wuluoy-creator/ZySU/releases). The Manager no longer offers a CI update channel. Updating its APK alone does not update the kernel module in the boot image.

Older APKs that still check `ZySU-Releases` need one manual update to an APK built with the new update address. Moving existing release assets does not change their embedded address. The old repository is retained for history and migration information; future releases are published here.

For source builds, follow [GitHub Actions and local builds](guide/workflow-build.md). Selecting Manager in **Build ZySU / 自选编译** builds its LKM, ksuinit, and ksud dependencies and repacks the APK. Download `Manager-arm64-v8a`; `Manager-gradle-arm64-v8a` is an intermediate artifact.

## Documentation

| Guide | Purpose |
| --- | --- |
| [Installation](guide/installation.md) | Image patching, direct installation, updates, and recovery |
| [Build workflows](guide/workflow-build.md) | Component selection, toolchains, signing, and artifacts |
| [LKM integration](guide/how-to-integrate.md) | Kernel constraints, DDK, and custom modules |
| [ksud CLI](ksud-cli.md) | Commands, arguments, and runtime behavior |
| [Ramdisk editor protocol](ramdisk-editor-protocol.md) | Binary requests and responses used by the Manager |
| [Userspace implementation](userspace-optimization.md) | Native architecture, build constraints, and host tests |
| [Chinese technical index](zh/README.md) | SUMH, OverlayFS, Magic Mount, Manager, diagnostics, and release guides |

## Source layout

| Directory | Contents |
| --- | --- |
| `kernel/` | Root, policy, hooks, SUMH, and kernel runtime |
| `uapi/` | Shared kernel/userspace interface definitions |
| `userspace/ksud/` | Daemon, su, module management, SUMHP, boot and partition tools |
| `userspace/ksuinit/` | Early module loading and handoff to the original init |
| `userspace/common/` | Shared native helpers |
| `manager/` | Android UI, JNI, resources, and unit tests |
| `js/` | KernelSU-compatible module WebUI JavaScript API |
| `scripts/`, `.github/` | Builds, checks, version resolution, and releases |

Host tests and source checks do not replace device boot, mount, or recovery tests. Issue reports should include device/firmware details, `uname -r`, KMI, all component versions, installation method, custom module use, reproduction steps, and relevant logs. Remove keys and private information before sharing logs. Follow [SECURITY.md](../SECURITY.md) for security reports.

## License and acknowledgements

See [kernel/LICENSE](../kernel/LICENSE) for GPL v2 and the repository [LICENSE](../LICENSE) for GPL v3. Individual file notices and bundled third-party licenses apply; the WebUI package declares Apache-2.0.

Thanks to [KernelSU](https://github.com/tiann/KernelSU), [SukiSU-Ultra](https://github.com/ShirkNeko/SukiSU-Ultra), [Magisk](https://github.com/topjohnwu/Magisk), and the bundled dependencies listed in the [third-party notes](../userspace/ksud/third_party/README.md).
