# Building with GitHub Actions

In your fork, open **Actions → Build YukiSU / 自选编译 → Run workflow**, select a branch and the components you need, then start the run. Enable workflows on the Actions page first if this is a new fork.

The defaults select **Manager APK**, set `kmi` to `all`, and enable `enable_yukizygisk`. This produces a complete Manager with all supported kernel modules and YukiZygisk. You do not need to select its dependencies individually.

## Select components

To build components separately, clear the default **Manager APK** selection, then select one or more components. Dependencies are added automatically, and each component is built once. Selecting nothing fails before compilation starts.

| Selection | Dependencies added automatically | Artifact |
| --- | --- | --- |
| Manager APK | ksud, selected LKM versions, ksuinit, and YukiZygisk when integration is enabled | `Manager-arm64-v8a` |
| LKM | None | `<KMI>-lkm` |
| ksud | Selected LKM versions, ksuinit, and YukiZygisk when integration is enabled | `ksud-aarch64-linux-android` |
| ksuinit | None | `ksuinit-aarch64-linux-android` |
| Standalone YukiZygisk | No other selectable component; includes both 64-bit and 32-bit payloads | `yukizygisk-aarch64-linux-android` |

`enable_yukizygisk` controls whether Manager/ksud embed YukiZygisk and whether LKM builds enable its kernel hooks. It is not a component selection: an LKM-only build with this option enabled does not also build the standalone payload.

The standalone **YukiZygisk** selection (`build_yukizygisk`) requests payload artifacts even when `enable_yukizygisk` is disabled. It does not itself embed the payload into Manager/ksud or enable LKM hooks.

## Select a kernel version

The default `kmi: all` builds all eight supported versions for a general-purpose package. Select a specific version, such as `android15-6.6`, from the dropdown to reduce the number of build jobs.

With a specific KMI selected, the resulting **Manager APK and ksud embed only that kernel module version**. Keep `all` when you need a package covering every supported version. KMI selection does not affect a standalone ksuinit or YukiZygisk build.

Other workflows using this entry point through `workflow_call` may also pass a comma-separated KMI list, such as `android14-6.1,android15-6.6`. Unsupported versions are rejected and duplicates are removed.

## Download and signing

The run's Summary shows your selections, dependencies, and job results. Download the required files from **Artifacts** at the bottom of the run page.

To install Manager, download **`Manager-arm64-v8a`** and extract its APK. `Manager-gradle-arm64-v8a` is an intermediate Gradle artifact without the embedded ksud; it is not the final installation package. `Manager-mappings-arm64-v8a` contains debugging mappings.

Manual builds work without configured signing keys. Without a production APK key, each run generates a temporary signing key, so **APKs from different runs cannot directly replace each other as updates**. For a consistent signing identity, configure these repository secrets under **Settings → Secrets and variables → Actions → Repository secrets**:

| Secret | Value |
| --- | --- |
| `KEYSTORE` | Base64-encoded JKS keystore file |
| `KEY_ALIAS` | Key alias |
| `KEYSTORE_PASSWORD` | Keystore password |
| `KEY_PASSWORD` | Key password |

GPG detached signatures are separate from Android APK signing. Without `GPG_PRIVATE_KEY`, LKM, ksud, and ksuinit still upload successfully without `.sig` files. Configure `GPG_PRIVATE_KEY` and `GPG_PASSPHRASE` to use your own GPG key for ordinary builds. Manager APKs also require the production APK keystore to receive a GPG signature; official nightly builds retain their pinned trusted signing identity.

Manual runs produce Actions artifacts and do not update the nightly distribution channel. Automatic complete builds still use **Build Manager**, preserving the existing update checks and nightly download entry point.

## Build efficiency

Only selected components and their dependencies are built. Different KMIs, independent components, and the Manager Gradle build can run in parallel, and native builds use multiple cores. Gradle build caching and userspace ccache reuse previous compilation results; artifact downloads fetch only the required files.

A new automatic build for the same branch or PR cancels an unfinished older build. Manual runs and tag releases remain independent; starting another manual run does not cancel them. Actual duration depends on component selection, KMI count, cache hits, and GitHub runner queues.
