"""Run script deadline and signal-mask regressions without Android or root.

Linux: python3 userspace/ksud/tests/run_script_wait_host_tests.py
Windows: python userspace/ksud/tests/run_script_wait_host_tests.py --zig /path/to/zig.exe
Linux cross-check: add --target x86_64-linux-musl --compile-only to the Zig command.
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
    parser.add_argument("--target", help="Optional Zig cross-compilation target")
    parser.add_argument("--compile-only", action="store_true")
    args = parser.parse_args()
    if args.target and not args.zig:
        parser.error("--target requires --zig")
    compiler = [args.zig, "c++"] if args.zig else [args.cxx]
    if args.target:
        compiler += ["-target", args.target]
    tests = Path(__file__).resolve().parent
    src = tests.parent / "src"
    source = (src / "module/module.cpp").read_text(encoding="utf-8")
    # Compile the production signal guard and script runner verbatim. Fake
    # process calls below inject races and failures on every supported host.
    begin = source.index("class SigchldBlock {")
    end = source.index("\n};", begin) + len("\n};")
    signal_guard = source[begin:end]
    begin = source.index("int run_script(")
    end = source.index("\n}", begin) + len("\n}")
    runner = source[begin:end]
    with tempfile.TemporaryDirectory(prefix="ksud-script-wait-tests-") as temporary:
        build = Path(temporary)
        (build / "script_wait_under_test.inc").write_text(
            signal_guard + "\n\n" + runner + "\n", encoding="utf-8")
        binary = build / ("script_wait_test.exe" if os.name == "nt" and not args.target
                          else "script_wait_test")
        subprocess.run([*compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(build), "-I" + str(src),
                        str(tests / "script_wait_test.cpp"), "-o", str(binary)], check=True)
        if args.compile_only:
            print("Script wait regressions compiled; execution skipped as requested.")
        else:
            subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    main()
