# GitHub Actions and local builds

Source, build definitions, and [ZySU Releases](https://github.com/wuluoy-creator/ZySU/releases) live together in the public `wuluoy-creator/ZySU` repository. Running its official Actions requires repository write access; you can also fork it and configure builds in your own repository. Check the actual run for its results.

## Select components

Open **Actions → Build ZySU / 自选编译 → Run workflow**. Choose a ref, components and KMI. The default selects Manager and `kmi=all`. [The planner](../../scripts/ci/plan_build.py) adds dependencies automatically:

| Selection | Added dependencies | Artifact |
| --- | --- | --- |
| Manager APK | ksud, selected LKMs, ksuinit | `Manager-arm64-v8a` |
| LKM | None | `<KMI>-lkm` |
| ksud | Selected LKMs, ksuinit | `ksud-aarch64-linux-android` |
| ksuinit | None | `ksuinit-aarch64-linux-android` |

Clear Manager when building a component separately. Selecting nothing fails during planning.

`all` selects `android14-6.1`, `android15-6.6`, `android16-6.12` and `android17-6.18`. A single selection embeds only that KMI's module in ksud and Manager. Reusable workflow calls also accept a comma-separated list; unsupported targets are rejected and duplicates removed. KMI selection does not change a standalone ksuinit build.

These are ARM64 build targets, not a record of tested devices. The kernel requires Linux 6.1 or newer; actual module compatibility also depends on the device kernel. See [kernel module builds](how-to-integrate.md).

## Download the final package

The run's Summary shows the plan, job results and artifact links. Install the APK from **`Manager-arm64-v8a`**. `Manager-gradle-arm64-v8a` is an intermediate APK before the workflow embeds the newly built ksud and resigns it. `Manager-mappings-arm64-v8a` contains debugging mappings.

The build chain is LKM + ksuinit → ksud → final APK. The Manager Gradle build can run alongside the native jobs; repacking waits for its dependencies. A job being scheduled or an artifact name appearing in this guide is not evidence of a successful build. Check the actual run and test the resulting package on the target device.

## APK signing and Manager authentication

Configure these repository secrets for a stable APK signing identity:

| Secret | Value |
| --- | --- |
| `KEYSTORE` | Base64-encoded JKS keystore |
| `KEY_ALIAS` | Key alias |
| `KEYSTORE_PASSWORD` | Keystore password |
| `KEY_PASSWORD` | Key password |

The LKM workflow exports the public certificate's size and SHA-256 from this keystore. Kbuild embeds that identity, and the repack step checks the final APK certificate against it. The kernel build receives public certificate metadata, not the private key.

Without a configured keystore, [the signing action](../../.github/actions/setup-manager-signing/action.yml) creates a temporary key. Different runs therefore do not provide a stable APK upgrade identity. The module retains its default trusted certificate; a temporary-signed Manager needs SuperKey authentication configured during image patching, or separately configured dynamic Manager trust. Updating only the APK does not replace a kernel module's trusted certificate.

`GPG_PRIVATE_KEY` and `GPG_PASSPHRASE` are optional detached-signature settings. They are separate from Android APK signing. LKM, ksud and ksuinit artifacts can be uploaded without `.sig` files; APK GPG signing also requires a configured APK keystore.

## Workflow entry points

| Workflow | Trigger and purpose |
| --- | --- |
| [Build ZySU / 自选编译](../../.github/workflows/build.yml) | Manual selection or reusable workflow call. Produces artifacts. |
| [Build Manager](../../.github/workflows/build-manager.yml) | Relevant path changes on `main`, `dev`, `feat/*` pushes and PRs targeting `main`/`dev`; also reusable. Requests a complete build. |
| [Build LKM for KernelSU](../../.github/workflows/build-lkm.yml) | Manual or reusable LKM-only build, including kernel version overrides. |
| [Release / 发布与版本管理](../../.github/workflows/release.yml) | Manual build/publish or automatic publishing on `v*` tag pushes. |

Path filters mean documentation-only edits do not necessarily trigger complete builds. New automatic branch/PR builds may cancel older runs for the same concurrency group; manual builds are independent, while Release runs are serialized.

## Publishing and versions

The manual Build entry exposes Manager and ksud version names/codes and the kernel version name. The Release entry additionally exposes the kernel version code, release tag and publish/prerelease switches. Leaving version inputs blank lets [resolve_versions.py](../../scripts/ci/resolve_versions.py) derive names from a release tag or Git and codes from Git history and published APK versions.

The Release workflow publishes directly to the public repository running it (`github.repository`), which is `wuluoy-creator/ZySU` for official builds. The publishing job uses the built-in `GITHUB_TOKEN` with `contents: write`; version checks read releases from the same repository. Configure the APK signing secrets above; no separate release repository or cross-repository token is needed. A fork's workflow publishes to that fork. A self-built Manager also needs its update address changed if it should use a separate update channel.

A manual Release run defaults to artifacts only. Enable `publish_release` and specify a tag to publish; pushing `v*` tags publishes automatically. The workflow keeps the release tag at the actual build commit in the same repository, creates a draft, uploads all prepared assets, and then makes it public. It does not overwrite an existing Release. Older installed APKs may need one manual update to switch their embedded update address; see [release and migration details (Chinese)](../zh/releases.md).

## Local build entry points

[build.sh](../../scripts/build.sh) and [build.bat](../../scripts/build.bat) build the complete ARM64 package locally. Both default to all four KMIs; `-k` selects one, `--skip-lkm` reuses matching `out/<KMI>_kernelsu.ko` files, `--clean` clears native CMake build directories, and `-i` requests APK installation through adb.

Use `bash scripts/build.sh --help` on Linux or `.\scripts\build.bat --help` on Windows to see the entry point's options. Local builds need the Android SDK/NDK, JDK, CMake, Ninja, Python 3 and Docker for the DDK stage. The Windows script uses Docker for LKMs and Windows tools for userspace and Manager; it reads the required NDK from [the version catalog](../../manager/gradle/libs.versions.toml). Preserve Git symbolic links as described in the [module guide](how-to-integrate.md).

Local signing uses `ZYSU_KEYSTORE`, `ZYSU_KEYSTORE_PASSWORD`, `ZYSU_KEY_ALIAS` and `ZYSU_KEY_PASSWORD`. Without signing configuration the release APK is unsigned. Unlike the Actions chain, the local scripts do not automatically derive and pass custom Manager certificate metadata into the LKM build. Configure Manager authentication accordingly when testing a local package.

The current Manager toolchain specifies JDK 21, Android SDK 37, Build Tools 36.1.0, NDK 30.0.16248370 and CMake 3.22.1. Native userspace targets API 31. Initialize submodules and make sure Docker can run `linux/amd64` containers and pull `ghcr.io/ylarod/ddk-min:<KMI>-20260828`.

On Linux / macOS, set `ANDROID_NDK_HOME`, configure `JAVA_HOME` and an Android SDK path for Gradle, and make `cmake`, `ninja`, `python3` and `docker` available. Windows accepts `ANDROID_SDK_ROOT` / `ANDROID_HOME`, `ANDROID_NDK_HOME`, `JAVA_HOME` and `PYTHON` overrides. Its script locates CMake / Ninja under the SDK and does not download missing SDK packages.

From the repository root, run `bash scripts/build.sh --kmi android14-6.1`, or `.\scripts\build.bat --kmi android14-6.1` on Windows. Native caches are reused by default; use `--clean` after changing toolchains when needed. The scripts restage generated assets in `userspace/ksud/assets/` and copy ksud into Manager as `libksud.so`.

| Local output | Path |
| --- | --- |
| LKM | `out/<KMI>_kernelsu.ko` |
| ksuinit | `userspace/ksuinit/build/ksuinit` |
| ksud | `userspace/ksud/build/ksud` |
| Final APK | `manager/app/build/outputs/renamed_apk/release/` |

An APK must be signed to install it. `-i` installs only the APK through adb; it does not flash the LKM into a boot image. Follow the [installation guide](installation.md) to synchronize ksud and patch the target image as needed.
