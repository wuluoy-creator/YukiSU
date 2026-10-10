# Installation, updates, and recovery

[Documentation home](../README.md) · [简体中文](../zh/guide/installation.md)

This guide describes the current Manager and ksud implementation. Installing the APK installs the Manager; obtaining kernel root also requires deploying a patched boot image containing a compatible LKM.

## Before installation

- Use an ARM64 device. Manager and userspace require Android 12 / API 31 or later.
- The kernel must be Linux 6.1 or newer. Packaged KMI targets are `android14-6.1`, `android15-6.6`, `android16-6.12`, and `android17-6.18`.
- Have a working method to write and recover the device's boot partitions, normally with an unlocked bootloader. Keep original images matching the installed firmware.
- Identify the image's partition and A/B slot. The Android release alone does not identify the KMI.

These read-only commands help record the device configuration:

```sh
adb shell uname -r
adb shell getprop ro.product.cpu.abi
adb shell getprop ro.build.version.sdk
adb shell getprop ro.boot.slot_suffix
```

The KMI matrix lists build targets, not devices certified to boot successfully.

## Get the Manager

Download an APK from [ZySU Releases](https://github.com/wuluoy-creator/ZySU/releases). For source builds, use the [build guide](workflow-build.md). The complete Actions APK is `Manager-arm64-v8a`; `Manager-gradle-arm64-v8a` is an intermediate artifact awaiting repacking.

Open the installation page from the home status area. Available methods depend on root access and A/B support.

## Standard LKM patching

The `boot-patch` path places ksuinit and the LKM in the ramdisk. ksuinit loads the module early in boot and hands off to the original init.

1. Obtain the stock `boot.img` or `init_boot.img` for the installed firmware, according to the device's ramdisk layout.
2. Select the local image in Manager. Use the embedded module or choose a custom `.ko` known to match the target kernel.
3. Manager tries to determine the target KMI. If detection fails, or an embedded LKM is selected and its list does not match, it asks for a manual selection. With a custom LKM, a detected KMI need not appear in the embedded list. Confirm the target kernel before selecting. An `init_boot` image has no kernel from which to read a KMI.
4. Configure authentication options as needed and run the patch.
5. Save the output and log. Local-file patching writes an image under the device's `Download` directory; it does not flash that selected file automatically.
6. Deploy the generated image to its corresponding partition and slot with the device's supported flashing method, then reboot.

The installation page can also probe an HTTPS firmware archive and extract available boot images. Use the partitions and formats actually reported by the UI, and verify that the firmware matches the device.

With existing root access, Manager offers direct installation to the current slot. A/B devices also offer installation to the inactive slot, intended for the target of an installed OTA. Verify that slot's image and KMI; do not substitute the currently running kernel's KMI. Direct installation writes partitions, so inspect the result before rebooting.

## Direct LKM injection into the boot kernel

The local-file and direct-install methods can enable this option, which uses `boot-patch-v2`.

| Detail | Standard LKM patching | Direct LKM injection |
| --- | --- | --- |
| Modified content | Ramdisk init, module, and configuration | ARM64 kernel Image inside boot and embedded module |
| Target | `boot` or `init_boot`, depending on the device | Kernel-bearing `boot.img` / `boot` only |
| KMI selection | Manager detects the target or requests manual selection | Embedded module selection reads the target kernel; no standard KMI dialog |
| File output argument | `--out` is a directory | `--output` / `--out` is a file |

Direct injection depends on kernel format, symbols, and ABI checks. It does not accept `init_boot` or `vendor_boot` as a replacement for boot, and it does not add `CONFIG_KSU=y` support. Broader image support in the ramdisk editor is separate from this installation path.

When switching methods, device-mode handling can also clean old ramdisk patches in the same slot. Inspect the actual partition operations and keep the corresponding originals.

## Authentication and first boot

Without a SuperKey, Manager recognition follows its signature configuration. Self-signed APKs need an appropriate trusted LKM configuration; see [LKM integration](how-to-integrate.md).

If configured, use the SuperKey set during patching. Signature bypass means SuperKey-only Manager authentication; it does not disable Linux module signature enforcement. “Allow shell” authorizes Android's shell UID for root, while enabling adbd is a separate option.

After rebooting, confirm kernel detection, authentication, UAPI compatibility, and the installed ksud version. Use the ksud card to synchronize the bundled program when needed. Grant root to applications before testing their access.

SUMHP is built in; ordinary module mounting does not require a separate mount MetaModule. `auto` chooses suitable OverlayFS, then Magic Mount. SUMH requires an explicit choice. OverlayFS / Magic Mount changes generally require a reboot; hot mounting is limited to eligible SUMH modules.

## Updates, removal, and recovery

Manager checks official releases in the public `wuluoy-creator/ZySU` repository. There is no CI update channel. APK, ksud, and the kernel module have separate versions: updating the APK alone does not update the module in the boot image. Synchronize ksud and repatch/reboot as needed. Recheck the target firmware and KMI after an OTA.

If an older installed APK still checks `ZySU-Releases`, manually install an APK from [ZySU Releases](https://github.com/wuluoy-creator/ZySU/releases) that was built with the new update address. This one-time update requires the same APK signing identity and a newer version code. Copying an old APK to the new repository does not change its embedded update address. Future releases are published in `ZySU`; the old repository is retained for history and migration information.

Restoring a boot image and permanently uninstalling are different operations. Restore calls `boot-restore -f`: standard ramdisk restoration can use a matching original backup or remove the patch; direct-injection restoration also validates matching backups.

Permanent uninstall removes userspace and module directories, attempts boot restoration, uninstalls Manager, and requests a reboot after about five seconds. The current implementation continues removal and reboot even if restoration fails, so prepare original images and an external recovery method before starting it. Removing the APK alone does not restore boot partitions.

If the device cannot boot, restore original images matching its firmware to the partitions and slot actually modified. For module-related failures, safe mode can help: the early kernel input hook marks safe mode after at least three volume-down press events, and userspace disables modules and skips their boot handling. Detection depends on hook timing and is not a replacement for image recovery.

## Diagnostics

Include firmware details, `uname -r`, KMI, all component versions, patch method, partition/slot, custom LKM use, and installation/boot logs in a report. In an authorized root shell:

```sh
/data/adb/ksud --version
/data/adb/ksud boot-info current-kmi
/data/adb/ksud debug info
/data/adb/ksud sumhp api system
```

`ksud install` installs userspace components; it is not a boot-image installation command. See the [CLI reference](../ksud-cli.md) for exact syntax.
