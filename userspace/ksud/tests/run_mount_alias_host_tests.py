"""Run SUMHP partition alias planning regressions without mounts or symlink privileges."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2] + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    parser.add_argument("--zig")
    args = parser.parse_args()
    compiler = [args.zig, "c++"] if args.zig else [args.cxx]
    tests = Path(__file__).resolve().parent
    mount = tests.parent / "sumhp/src/mount"
    with tempfile.TemporaryDirectory(prefix="sumhp-alias-tests-") as temporary:
        build = Path(temporary)
        source = (mount / "partition_alias.hpp").read_text(encoding="utf-8")
        # Only the filesystem adapter changes: the host models links without
        # administrator/developer-mode privileges on Windows.
        alias = function(source, "inline std::optional<std::filesystem::path> module_partition_alias(")
        alias = alias.replace("namespace fs = std::filesystem;", "namespace fs = fixture;")
        (build / "partition_alias_under_test.inc").write_text(alias, encoding="utf-8")
        source = (mount / "magic_mount.cpp").read_text(encoding="utf-8")
        (build / "magic_alias_under_test.inc").write_text(
            function(source, "bool collect_into(") +
            function(source, "std::optional<Node> collect_module_files("), encoding="utf-8")
        source = (mount / "overlayfs.cpp").read_text(encoding="utf-8")
        (build / "overlay_alias_under_test.inc").write_text("\n".join(
            function(source, signature) for signature in [
                "bool is_sub_partition(", "void plan_partition_root(",
                "std::map<std::string, std::vector<std::string>> plan_overlays(",
                "bool validate_partition_tree("]), encoding="utf-8")
        binary = build / ("mount_partition_alias_test.exe" if os.name == "nt"
                          else "mount_partition_alias_test")
        subprocess.run([*compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(build), str(tests / "mount_partition_alias_test.cpp"),
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    main()
