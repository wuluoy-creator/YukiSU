# ksud command-line reference

`ksud` is ZySU's C++ userspace entry point for modules, boot images, kernel features and the built-in SUMHP controller. Installed devices use `/data/adb/ksud`; the manager also invokes its packaged `libksud.so` executable. Operations on modules, kernel state and partitions require the corresponding root/kernel access.

This reference follows [cli_args.cpp](../userspace/ksud/src/cli_args.cpp), [cli.cpp](../userspace/ksud/src/cli.cpp) and their implementations. Consult the help shipped with your binary when using another version:

```sh
ksud --help
ksud help boot-patch
ksud sumhp module set-mode --help
```

## Arguments and output

| Option | Meaning |
| --- | --- |
| `-h`, `--help` | Show help without executing the selected operation |
| `--color auto\|always\|never` | Diagnostic colors; default `auto` |
| `--verbose` | Include internal diagnostic logs |
| Top-level `-v`, `-V`, `--version` | Print the userspace and UAPI versions |

Global options work before or after commands and operands. The shared parser rejects unknown options, duplicate command options, missing values and extra operands. Use `--` before operands beginning with `-`, and `--option=-value` for option values beginning with `-`. Everything after the module filename in `insmod` is passed to the loader verbatim.

Shared-parser errors exit with status `2`; help exits with `0`. Runtime statuses depend on the command. Diagnostics go to stderr; results and JSON go to stdout, which is not uniformly JSON. Automatic color requires a terminal, nonempty `TERM` other than `dumb`, and no nonempty `NO_COLOR`. Explicit color selection takes precedence.

Embedded `busybox`, `magiskboot`, `bootctl`, `resetprop`, `mkbootfs` and `readelf` are dispatched before the shared parser when compiled in. For example, `ksud magiskboot --help` uses magiskboot's grammar. Invoking the binary as `su` selects the separate su parser; there is no `ksud su` subcommand.

## Modules and features

| Command | Purpose / options |
| --- | --- |
| `module install <ZIP>` | Run the module installer |
| `module list` | List modules as JSON |
| `module enable <ID>`, `module disable <ID>` | Change enabled state |
| `module uninstall <ID>`, `module undo-uninstall <ID>` | Mark removal or cancel the mark |
| `module action <ID>` | Run the module action script |
| `module config get <KEY>`, `module config list` | Read configuration for `KSU_MODULE` |
| `module config set <KEY> <VALUE>` | Save a value; `-t` / `--temp` selects temporary configuration |
| `module config delete <KEY>`, `module config clear` | Remove values; also accept `-t` / `--temp` |
| `feature list`, `feature get <ID>`, `feature check <ID>` | Inspect values and support |
| `feature set <ID> <VALUE>` | Set an unsigned 64-bit value at runtime |
| `feature set-save <ID> <VALUE>` | Set and persist a value |
| `feature load`, `feature save` | Apply saved values or save current values |

Module commands enter init's mount namespace. `module config` requires `KSU_MODULE` to contain the module ID, normally supplied by module scripts. Temporary values override persistent ones and are cleared during boot. Keys cannot contain `=` or line breaks; values cannot contain line breaks. Reboot to apply normal module enable/disable/removal changes to the running system.

## Userspace installation and boot images

`install [--magiskboot <PATH>] [--libadbroot <PATH>]` installs userspace components; it does not itself patch a boot image. `uninstall [--magiskboot <PATH>]` runs the uninstall flow. Boot patchers use embedded magiskboot; their `--magiskboot` argument and the uninstaller's equivalent are retained for compatibility.

`uninstall` removes userspace and modules, attempts boot restoration, uninstalls Manager and requests a reboot after about five seconds. It currently continues removal and reboot even if restoration fails. Use `boot-restore` for image restoration alone, and keep matching original images available before uninstalling.

```sh
ksud boot-info current-kmi
ksud boot-info supported-kmis
ksud boot-info available-partitions
ksud boot-info default-partition
ksud boot-info target-kmi --boot /path/to/boot.img
```

Other queries are `boot-info is-ab-device`, `boot-info slot-suffix [--ota]` and `boot-info target-kmi [--ota]`. `target-kmi` rejects combining `--boot` with `--ota`.

### Ramdisk patching

`boot-patch` uses the ksuinit/ramdisk path. Without `--boot`, it detects the device partition. Partition writes require `--flash`; otherwise it produces an image file.

| Option | Meaning |
| --- | --- |
| `-b`, `--boot <IMAGE>` | Input image instead of the detected partition |
| `-f`, `--flash` | Write the result to the selected partition |
| `-o`, `--out <DIRECTORY>` | Output directory, not an image filename |
| `--out-name <NAME>` | Output image filename |
| `-m`, `--module <KO>` | Use an explicit LKM instead of an embedded asset |
| `-k`, `--kernel <IMAGE>` | Replace the kernel |
| `-i`, `--init <FILE>` | Supply replacement init |
| `-u`, `--ota` | Target the inactive A/B slot |
| `--partition boot\|init_boot` | Select the partition |
| `--kmi <KMI>` | Override KMI for asset selection |
| `--backup` | Back up the stock image |
| `-s`, `--superkey <KEY>` | Set SuperKey |
| `--signature-bypass` | With SuperKey, select key-only manager authentication instead of APK signature + key |
| `--allow-shell` | Allow Android shell UID to request root with the default profile |
| `--no-custom-rc` | Skip custom init.rc injection |
| `--enable-adbd` | Enable the adbd root setup |
| `--adb-debug-prop <FILE>` | Embed adb debug properties |
| `--magiskboot <PATH>` | Compatibility argument; embedded tool is used |

```sh
ksud boot-patch --boot /path/to/init_boot.img --out /path/to/output --out-name patched.img
```

Select the image for the actual firmware and installation method. A manually supplied KMI does not make an arbitrary LKM compatible. See the [installation guide](guide/installation.md).

### Direct kernel-image injection

`boot-patch-v2` injects the LKM into a kernel image. Its two modes are:

```sh
ksud boot-patch-v2 --boot /path/to/boot.img --output /path/to/patched.img
ksud boot-patch-v2 --flash --ota
```

File mode requires `--boot` (`-b`) and `--output` (`-o`, or `--out`); here the output is a **file**. Device mode uses `--flash`, optionally `--ota`, and rejects explicit input/output paths. `--ota` requires `--flash`. This path needs a supported AOSP **boot image containing a kernel** and rejects `init_boot` and `vendor_boot` as patch targets.

Additional options are `--module` / `-m`, `--superkey`, `--signature-bypass`, `--allow-shell`, `--enable-adbd`, `--magiskboot`, `--force` and `--no-reuse`. `--force` permits replacing an existing output, never an input. `--no-reuse` forces full kernel analysis instead of reusing compatible injection metadata. In both patch paths, `--signature-bypass` selects SuperKey-only manager authentication; without `--superkey`, it emits a warning and is ignored. It does not bypass Linux module-signature verification.

`boot-restore` accepts `--boot` / `-b`, `--flash` / `-f`, `--out-name` and `--magiskboot`. It uses available restore data; retain the original firmware image for recovery.

`ramdisk-editor <CPIO>` and `boot-ramdisk-editor <SOURCE> <OUTPUT>` are binary protocol sessions, not shell editors. See [Ramdisk editor protocol](ramdisk-editor-protocol.md) for image and wire-format limits.

## SUMHP and mount backends

SUMHP is built into ksud. State lives under `/data/adb/ksu/sumhp`; logs are written to `/data/adb/ksu/log/sumhp.log`.

```sh
ksud sumhp config show
ksud sumhp api backends
ksud sumhp api meta --control-only
ksud sumhp module list --all
ksud sumhp module set-mode example.module magic
```

| Command group | Operations |
| --- | --- |
| `sumhp config` | `show`, `gen`, `merge-json <JSON>`, `apply`, `sync-partitions` |
| `sumhp api` | `system`, `storage`, `sumh`, `features`, `hooks`, `mounts`, `backends`, `meta [--control-only]` |
| `sumhp daemon` | `status`, `start`, `serve`, `ping`, `stop`, `call <COMMAND> [ARGS...]` |
| `sumhp module` | `list [--all]`, `set-mode <ID> <MODE>`, `add-rule <ID> <ABSOLUTE-PATH> <MODE>`, `remove-rule <ID> <ABSOLUTE-PATH>`, `check-conflicts`, `normalize <PATH>` |
| `sumhp module` runtime operations | `add` / `hot-mount <ID>`, `delete` / `hot-unmount <ID>`, `mount-all`, `unmount` |
| `sumhp sumh` | `version`, `list`, `features`, `clear`, `hide-path <ABSOLUTE-PATH>`, `delete-rule <ABSOLUTE-PATH>`, `fix-mounts`, `hide-overlay-xattrs <ABSOLUTE-PATH>` |
| `sumhp sumh` feature controls | `mount-hide off\|on\|normal\|aggressive`, `maps-spoof off\|on`, `statfs-spoof off\|on` |
| `sumhp sumh maps` | `clear`, `add <TARGET-INO> <TARGET-DEV> <SPOOF-INO> <SPOOF-DEV> <PATH>` |
| `sumhp sumh kernel-build` | `show`, `set <RELEASE> <VERSION>`, `reset` |
| `sumhp hide` | `list`, `apply`, `add <ABSOLUTE-PATH>`, `remove <ABSOLUTE-PATH>` |
| `sumhp debug` | `enable`, `disable`, `stealth enable\|disable` |
| `sumhp recovery` | `status`, `reset` |

Module modes are `auto`, `sumh`, `overlay`, `magic` and `none`; path rules also accept `hide`. A non-`auto` global `mount_backend` overrides individual module choices. `auto` follows **OverlayFS → Magic Mount → none**, selecting Magic Mount for files directly under a partition root, such as `system/build.prop`. SUMH is selected explicitly. Unavailable requested backends can resolve to fallback backends; inspect the actual plan and logs after boot.

Hot operations replay only modules assigned to SUMH in the current boot. Changing a mode does not migrate live OverlayFS/Magic Mount state; reboot for backend changes. `mount-all` is restricted to the boot path and runs at most once per boot. Recovery protection disables mounting after three unconfirmed boot attempts. Inspect `recovery status`, fix the affected modules, then `recovery reset` and reboot.

`config merge-json` validates keys, types, enums, paths and numeric ranges. `config gen` writes defaults. `config apply` applies supported runtime settings; changing a mount layout still requires reboot. For kernel build overrides, either `set` operand may be `default`, but not both; `reset` restores both. See [kernel build overrides](zh/guide/sumh-kernel-build.md).

The parser still accepts `mount-hide aggressive`, but the current kernel rejects this retired mode and does not advertise its capability. Applying an older saved aggressive configuration falls back to normal mode. Direct feature controls affect runtime state; use saved configuration and `config apply` for persistent settings.

Daemon control requests use a 5-second client timeout; other requests use 60 seconds. Timeout does not cancel an executing operation. Lost responses do not trigger replay; inspect state before retrying changes. Automatic startup/retry is limited to missing/refused socket connections. The newline-terminated transport limits requests to 64 KiB and client responses to 1 MiB; the daemon allows 5 seconds to receive or send a frame.

Legacy state migration runs before daemon startup or kernel-build restoration. Missing destination files are imported; existing destination data wins and conflicts remain in the old directory. Read, JSON or write errors stop migration. Default paths and the old backend name are updated while custom paths remain. Reboot after replacing an already-running controller or one that mounted modules during this boot.

## Partition and kernel tools

| Command | Options / purpose |
| --- | --- |
| `flash ak3 <ZIP>` | `--slot <SLOT>`, `--log <FILE>`, `--use-mkbootfs`, `--verbose` / `-v`; executes the archive installer |
| `flash ak3-info <ZIP>` | Inspect an archive |
| `flash image <IMAGE> <PARTITION>` | `--slot <SLOT>`; writes immediately |
| `flash backup <PARTITION> <OUTPUT>` | `--slot <SLOT>` |
| `flash list` | `--slot <SLOT>`, `--all` |
| `flash info <PARTITION>`, `flash kernel` | `--slot <SLOT>` |
| `flash slots`, `flash boot-info` | Query slot information |
| `flash map <SLOT>` | Map logical partitions |
| `flash avb [disable]` | Query AVB; `disable` modifies verification configuration |
| `insmod <KO> [PARAMS...]` | Load an LKM with kallsyms support; also `debug insmod` |
| `umount add <MOUNT>` | `--flags` / `-f <UINT32>`; register an unmount path |
| `umount del <MOUNT>`, `umount remove <MOUNT>` | Delete an unmount entry |
| `umount list`, `save`, `apply`, `clear-custom` | Inspect/persist custom unmount state |
| `kernel umount add <MOUNT>` | `--flags` / `-f <UINT32>` |
| `kernel umount del <MOUNT>`, `kernel umount wipe` | Modify the kernel unmount list |
| `kernel nuke-ext4-sysfs <MOUNT>`, `kernel notify-module-mounted` | Low-level integration |

`SLOT` accepts `a`, `b`, `_a`, `_b`; omission selects the current slot. `flash image` and `flash ak3` do not require an additional `--flash` flag.

Other command families are `sepolicy patch|check <POLICY>`, `sepolicy apply <FILE>`, `profile get-sepolicy|set-sepolicy|get-template|set-template|delete-template|list-templates`, `dynamic get-sign|set-hash|set-apk|set-uid|list|del|clear`, `su-path get|set|reset`, and `debug set-manager|su|version|info|mark|sulogd`. Consult subcommand help for operands. `dynamic get-sign` takes exactly one APK path or `--uid <UID>` and supports `--json`; signature hashes contain 64 hex digits. `su-path set <ABSOLUTE-PATH>` supports `--json`.

`post-fs-data`, `services`, `boot-completed`, `initrc refresh` and `sulogd` are boot/runtime integration entry points.

## Script waits and interactive shells

Common, metamodule and module `post-fs-data`/`post-mount` scripts share one 35-second waiting deadline. Timeout stops waiting, not the script. Later scripts still start. Service and boot-completed scripts are not waited for. Action, uninstall and metamodule mount scripts can wait until completion, so 35 seconds is not a bound on the entire boot handler.

Interactive system mksh and bundled BusyBox ash use `/data/adb/ksu/bin/shellrc.sh` when readable. Personal settings belong in `/data/adb/ksu/.shellrc`, sourced last. An explicit `ENV`, exported `PS1`, `su -p` or custom shell keeps its configuration; `su -c` does not receive interactive startup. `/data/adb/ksu/.ksurc` is feature configuration, not the personal shell startup file.
