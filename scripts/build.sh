#!/usr/bin/env bash
# ZySU local build: DDK LKMs -> ksuinit -> ksud -> Manager App
# Signing env: ZYSU_KEYSTORE, ZYSU_KEYSTORE_PASSWORD, ZYSU_KEY_ALIAS, ZYSU_KEY_PASSWORD
# Usage: ./scripts/build.sh [-k KMI] [--clean] [--skip-lkm] [-i] [-h]
# Without --kmi, all supported LKM targets are built and embedded in ksud.
# --clean deletes Native CMake build directories before building.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_DIR="$REPO_ROOT/out"
# Keep this list in sync with build-lkm.yml and ksud.yml.
SUPPORTED_KMIS=(
	android14-6.1
	android15-6.6
	android16-6.12
	android17-6.18
)
KMI_TARGETS=("${SUPPORTED_KMIS[@]}")
ANDROID_ABI="arm64-v8a"
CLEAN_BUILD=false
SKIP_LKM=false
DDK_RELEASE="20260828"
DO_INSTALL=false

while [[ $# -gt 0 ]]; do
	case "$1" in
	-k | --kmi)
		KMI_TARGETS=("$2")
		shift 2
		;;
	--clean)
		CLEAN_BUILD=true
		shift
		;;
	--skip-lkm)
		SKIP_LKM=true
		shift
		;;
	-i | --install)
		DO_INSTALL=true
		shift
		;;
	-h | --help)
		head -5 "$0" | tail -n +2 | sed 's/^# \?//'
		exit 0
		;;
	*)
		echo "Unknown option: $1"
		exit 1
		;;
	esac
done

for kmi in "${KMI_TARGETS[@]}"; do
	if [[ ! "$kmi" =~ ^[A-Za-z0-9._-]+$ || " ${SUPPORTED_KMIS[*]} " != *" $kmi "* ]]; then
		echo "Unsupported KMI/DDK target: $kmi (requires Linux 6.1 or newer)"
		echo "Supported targets: ${SUPPORTED_KMIS[*]}"
		exit 1
	fi
done

# Android API floor. CMake overrides the versioned NDK wrapper's --target with one
# it derives from CMAKE_SYSTEM_VERSION, so that variable -- not the wrapper path --
# is what actually selects the API level. Enforced at compile time by
# userspace/common/api_floor.hpp.
ANDROID_API=31
ANDROID_TARGET=aarch64-linux-android${ANDROID_API}

detect_ndk_host() {
	if [[ -z "${ANDROID_NDK_HOME:-}" ]]; then
		echo "ANDROID_NDK_HOME is required"
		exit 1
	fi
	local prebuilt="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt"
	if [[ -d "$prebuilt/darwin-x86_64" ]]; then
		echo "darwin-x86_64"
	elif [[ -d "$prebuilt/darwin-arm64" ]]; then
		echo "darwin-arm64"
	elif [[ -d "$prebuilt/linux-x86_64" ]]; then
		echo "linux-x86_64"
	else
		echo "Cannot detect NDK prebuilt toolchain"
		exit 1
	fi
}

detect_jobs() {
	if command -v nproc >/dev/null 2>&1; then
		nproc --all
	elif command -v getconf >/dev/null 2>&1; then
		getconf _NPROCESSORS_ONLN
	elif command -v sysctl >/dev/null 2>&1; then
		sysctl -n hw.logicalcpu
	else
		echo 8
	fi
}

prepare_build_dir() {
	local build_dir="$1"

	if [[ "$CLEAN_BUILD" == "true" ]]; then
		case "$build_dir" in
		"$REPO_ROOT"/*/build | "$REPO_ROOT"/*/build-*) ;;
		*)
			echo "Refusing to clean unsafe build directory: $build_dir"
			exit 1
			;;
		esac
		rm -rf -- "$build_dir"
	fi
	mkdir -p "$build_dir"
}

NDK_HOST=$(detect_ndk_host)
TOOLCHAIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$NDK_HOST"
MAKE_JOBS=$(detect_jobs)

echo "=== ZySU local build ==="
echo "KMIs: ${KMI_TARGETS[*]} | ABI: $ANDROID_ABI | NDK: $ANDROID_NDK_HOME"
if [[ "$CLEAN_BUILD" == "true" ]]; then
	echo "Native cache: clean rebuild"
else
	echo "Native cache: reuse build directories"
fi
echo ""

if [[ "$SKIP_LKM" != "true" ]]; then
	echo ">>> [1/4] Build KernelSU LKMs (DDK) ..."
	mkdir -p "$OUT_DIR"
	for kmi in "${KMI_TARGETS[@]}"; do
		echo "    Building LKM: $kmi"
		docker run --platform linux/amd64 --rm -v "$REPO_ROOT:/src" -w /src \
			"ghcr.io/ylarod/ddk-min:${kmi}-${DDK_RELEASE}" \
			bash -c "cd kernel && test -f include/uapi/supercall.h && \
		             make clean && \
		             CONFIG_KSU=m CONFIG_KSU_SUPERKEY=y CC=clang make -j${MAKE_JOBS} && \
		             mkdir -p /src/out && cp kernelsu.ko /src/out/${kmi}_kernelsu.ko && \
		             (llvm-strip -d /src/out/${kmi}_kernelsu.ko 2>/dev/null || true)"
		echo "    LKM: $OUT_DIR/${kmi}_kernelsu.ko"
	done
else
	echo ">>> [1/4] Skip LKM builds; reuse outputs"
fi

echo ">>> [2/4] Build ksuinit ..."
KSUINIT_DIR="$REPO_ROOT/userspace/ksuinit"
prepare_build_dir "$KSUINIT_DIR/build"
cd "$KSUINIT_DIR/build"

export CC="$TOOLCHAIN/bin/${ANDROID_TARGET}-clang"
export CXX="$TOOLCHAIN/bin/${ANDROID_TARGET}-clang++"
export AR="$TOOLCHAIN/bin/llvm-ar"
export RANLIB="$TOOLCHAIN/bin/llvm-ranlib"

cmake .. \
	-G Ninja \
	-DCMAKE_SYSTEM_NAME=Android \
	-DCMAKE_ANDROID_ARCH_ABI="$ANDROID_ABI" \
	-DCMAKE_ANDROID_NDK="$ANDROID_NDK_HOME" \
	-DCMAKE_SYSTEM_VERSION="$ANDROID_API" \
	-DCMAKE_C_COMPILER="$CC" \
	-DCMAKE_CXX_COMPILER="$CXX" \
	-DCMAKE_BUILD_TYPE=Release

ninja
echo "    ksuinit built"

echo ">>> [3/4] Build ksud ..."
KSUD_ASSETS="$REPO_ROOT/userspace/ksud/assets"
mkdir -p "$KSUD_ASSETS"
mkdir -p "$OUT_DIR"
find "$KSUD_ASSETS" -maxdepth 1 -type f -name '*.ko' -delete
rm -f -- "$KSUD_ASSETS/ksuinit" "$KSUD_ASSETS/su"

for kmi in "${KMI_TARGETS[@]}"; do
	lkm="$OUT_DIR/${kmi}_kernelsu.ko"
	if [[ ! -f "$lkm" ]]; then
		echo "Required LKM not found: $lkm"
		exit 1
	fi
	cp "$lkm" "$KSUD_ASSETS/"
done
echo "    staged LKMs: ${KMI_TARGETS[*]}"

cp "$KSUINIT_DIR/build/ksuinit" "$KSUD_ASSETS/"

KSUD_DIR="$REPO_ROOT/userspace/ksud"
prepare_build_dir "$KSUD_DIR/build"
cd "$KSUD_DIR/build"

cmake .. \
	-G Ninja \
	-DCMAKE_SYSTEM_NAME=Android \
	-DCMAKE_ANDROID_ARCH_ABI="$ANDROID_ABI" \
	-DCMAKE_ANDROID_NDK="$ANDROID_NDK_HOME" \
	-DCMAKE_SYSTEM_VERSION="$ANDROID_API" \
	-DCMAKE_C_COMPILER="$CC" \
	-DCMAKE_CXX_COMPILER="$CXX" \
	-DCMAKE_BUILD_TYPE=Release

# ksuinit and the .ko assets are staged into assets/ before this configure.
ninja
echo "    ksud built"

echo ">>> [4/4] Build Manager App ..."
MANAGER_DIR="$REPO_ROOT/manager"
JNILIBS="$MANAGER_DIR/app/src/main/jniLibs/$ANDROID_ABI"
mkdir -p "$JNILIBS"
cp "$KSUD_DIR/build/ksud" "$JNILIBS/libksud.so"

# Signing is passed through env, not gradle.properties.
if [[ -n "${ZYSU_KEYSTORE:-}" && -n "${ZYSU_KEYSTORE_PASSWORD:-}" && -n "${ZYSU_KEY_ALIAS:-}" && -n "${ZYSU_KEY_PASSWORD:-}" ]]; then
	export KEYSTORE_FILE="$ZYSU_KEYSTORE"
	export KEYSTORE_PASSWORD="$ZYSU_KEYSTORE_PASSWORD"
	export KEY_ALIAS="$ZYSU_KEY_ALIAS"
	export KEY_PASSWORD="$ZYSU_KEY_PASSWORD"
	export ORG_GRADLE_PROJECT_KEYSTORE_FILE="$ZYSU_KEYSTORE"
	export ORG_GRADLE_PROJECT_KEYSTORE_PASSWORD="$ZYSU_KEYSTORE_PASSWORD"
	export ORG_GRADLE_PROJECT_KEY_ALIAS="$ZYSU_KEY_ALIAS"
	export ORG_GRADLE_PROJECT_KEY_PASSWORD="$ZYSU_KEY_PASSWORD"
fi

cd "$MANAGER_DIR"
./gradlew assembleRelease --build-cache --no-daemon -PABI="$ANDROID_ABI"
echo "    APK built"

APK_DIR="$MANAGER_DIR/app/build/outputs/renamed_apk/release"
echo ""
echo "=== Build complete ==="
echo "APK: $APK_DIR"
ls -la "$APK_DIR"/*.apk 2>/dev/null || true
echo ""

if [[ "$DO_INSTALL" == "true" ]]; then
	apk_files=("$APK_DIR"/*.apk)
	APK_FILE=""
	if [[ ${#apk_files[@]} -gt 0 ]]; then
		APK_FILE="${apk_files[0]}"
	fi
	if [[ -n "$APK_FILE" ]]; then
		echo ">>> Install to device ..."
		adb install -r "$APK_FILE" && echo "APK installed" || echo "APK install failed"
		echo ""
		echo "ksud is embedded in the APK; sync it from the app before reboot."
	fi
else
	echo "Install: adb install -r $APK_DIR/*.apk"
	echo "Or: ./scripts/build.sh --skip-lkm -i"
	echo ""
	echo "After installing, sync ksud from the app before reboot."
fi
