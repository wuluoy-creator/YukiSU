"""Run boot-property regressions without Android or root.

Linux: python3 userspace/ksud/tests/run_hide_bootloader_host_tests.py
Windows: python userspace/ksud/tests/run_hide_bootloader_host_tests.py --zig /path/to/zig.exe
"""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    parser.add_argument("--zig", help="Use Zig's bundled C++ compiler")
    parser.add_argument("--compile-only", action="store_true")
    args = parser.parse_args()
    compiler = [args.zig, "c++"] if args.zig else [args.cxx]
    tests = Path(__file__).resolve().parent
    repo = tests.parents[2]
    source = (tests.parent / "src/core/hide_bootloader.cpp").read_text(encoding="utf-8")
    # Keep the complete production implementation while replacing platform
    # dependencies with the fixture's feature/property interfaces.
    implementation = source[source.index("namespace ksud {"):]
    with tempfile.TemporaryDirectory(prefix="ksud-hide-bootloader-tests-") as temporary:
        build = Path(temporary)
        (build / "hide_bootloader_under_test.inc").write_text(
            implementation, encoding="utf-8")
        binary = build / ("hide_bootloader_test.exe" if os.name == "nt"
                          else "hide_bootloader_test")
        subprocess.run([*compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(build), "-I" + str(repo),
                        str(tests / "hide_bootloader_test.cpp"), "-o", str(binary)],
                       check=True)
        if args.compile_only:
            print("Boot-property regressions compiled; execution skipped as requested.")
        else:
            subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    main()
