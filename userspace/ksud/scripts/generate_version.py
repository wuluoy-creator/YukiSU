#!/usr/bin/env python3
import subprocess
import sys
import os
import re
from pathlib import Path


# Keep ksud's numeric version aligned with manager/build.gradle.kts and
# kernel/Kbuild. The offset leaves room for the historical version range.
VERSION_CODE_BASE = 10000 - 3135
VERSION_NAME_PATTERN = re.compile(
    r"v?[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?"
)


def version_name_override():
    value = os.environ.get("ZYSU_KSUD_VERSION_NAME", "")
    if not value:
        return None
    if len(value) > 80 or VERSION_NAME_PATTERN.fullmatch(value) is None:
        raise ValueError(
            "ZYSU_KSUD_VERSION_NAME must be a version such as 1.8.0 or "
            "v1.8.0-rc.1, at most 80 characters"
        )
    return value.removeprefix("v")


def version_code_override():
    value = os.environ.get("ZYSU_KSUD_VERSION_CODE", "")
    if not value:
        return None
    normalized = value.lstrip("0") or "0"
    if re.fullmatch(r"[0-9]+", value) is None or len(normalized) > 10:
        raise ValueError("ZYSU_KSUD_VERSION_CODE must be an integer from 1 to 2100000000")
    code = int(normalized)
    if not 1 <= code <= 2100000000:
        raise ValueError("ZYSU_KSUD_VERSION_CODE must be an integer from 1 to 2100000000")
    return code


def normalize_version_name(describe):
    describe = describe.strip()
    # Match the manager's git-describe normalization, including prerelease tags.
    return re.sub(r"-\d+-g", "-", describe).removeprefix("v")


DIRTY_VERSION_PATTERN = re.compile(
    rb"(?:v?[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?"
    rb"|[0-9a-fA-F]{8})-nc(?:@ZySU)?(?:[^0-9A-Za-z]|$)"
)


def git_has_changes(repo_root, paths):
    try:
        result = subprocess.run(
            [
                "git",
                "-C",
                str(repo_root),
                "-c",
                "core.autocrlf=true",
                "status",
                "--porcelain",
                "--untracked-files=normal",
                "--",
                *paths,
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            check=False,
        )
        return result.returncode == 0 and bool(result.stdout.strip())
    except OSError:
        return False


def has_dirty_version(path):
    try:
        return DIRTY_VERSION_PATTERN.search(path.read_bytes()) is not None
    except OSError:
        return False


def has_kernel_nc(repo_root):
    kernel_assets = (
        repo_root / "userspace" / "ksud" / "assets",
        repo_root / "out",
    )
    built_kernel_nc = any(
        has_dirty_version(path)
        for directory in kernel_assets
        for path in directory.glob("*_kernelsu.ko")
        if path.is_file()
    )
    return built_kernel_nc or git_has_changes(repo_root, ("kernel", "uapi"))


def has_ksud_nc(repo_root):
    return has_kernel_nc(repo_root) or git_has_changes(repo_root, ("userspace/ksud",))


def get_git_version():
    # Validate before the Git fallback: invalid release inputs must fail the build.
    name_override = version_name_override()
    code_override = version_code_override()
    repo_root = Path(__file__).resolve().parents[3]
    dirty = has_ksud_nc(repo_root)

    version_code = code_override
    if version_code is None:
        try:
            count_output = subprocess.check_output(
                ["git", "rev-list", "--count", "HEAD"],
                stderr=subprocess.DEVNULL,
                text=True,
            ).strip()
            version_code = int(count_output) + VERSION_CODE_BASE
        except (OSError, subprocess.CalledProcessError, ValueError):
            print("Warning: Failed to get git version code, using default", file=sys.stderr)
            version_code = 12000

    version_name = name_override
    if version_name is None:
        try:
            tag_output = subprocess.check_output(
                ["git", "describe", "--tags", "--always", "--abbrev=8"],
                stderr=subprocess.DEVNULL,
                text=True,
            ).strip()
            version_name = normalize_version_name(tag_output)
        except (OSError, subprocess.CalledProcessError):
            try:
                version_name = subprocess.check_output(
                    ["git", "rev-parse", "--short=8", "HEAD"],
                    stderr=subprocess.DEVNULL,
                    text=True,
                ).strip()
            except (OSError, subprocess.CalledProcessError):
                print("Warning: Failed to get git version name, using default", file=sys.stderr)
                version_name = "1.2.0"

    if dirty and not version_name.endswith("-nc"):
        version_name += "-nc"
    return version_code, version_name

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: generate_version.py <output_file>")
        sys.exit(1)
    
    output_file = sys.argv[1]
    try:
        code, name = get_git_version()
    except ValueError as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
    
    # Generate C++ source file
    content = f'''#include "defs.hpp"

namespace ksud {{

// Auto-generated at build time
const char* const VERSION_CODE = "{code}";
const char* const VERSION_NAME = "{name}";

}}  // namespace ksud
'''
    
    # Only write if changed to avoid unnecessary rebuilds
    if os.path.exists(output_file):
        with open(output_file, 'r') as f:
            if f.read() == content:
                sys.exit(0)
    
    with open(output_file, 'w') as f:
        f.write(content)
    
    print(f"Generated version: {name} ({code})")
