"""Run ksuinit parser, logging and init handoff regressions without root.

Linux: python3 userspace/ksuinit/tests/run_host_tests.py --cxx clang++
Windows: python userspace/ksuinit/tests/run_host_tests.py --zig /path/to/zig.exe
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    parser.add_argument("--zig", type=Path)
    parser.add_argument("--sanitize", action="store_true",
                        help="Enable AddressSanitizer and UBSan on supported host compilers")
    args = parser.parse_args()
    compiler = [str(args.zig), "c++"] if args.zig else [args.cxx]
    tests = Path(__file__).resolve().parent
    src = tests.parent / "src"

    with tempfile.TemporaryDirectory(prefix="ksuinit-host-tests-") as temporary:
        build = Path(temporary)
        if os.name == "nt":
            if not args.zig:
                parser.error("Windows tests require --zig for the ELF header")
            # Use the standalone ELF definitions without adding Linux libc headers.
            shutil.copyfile(args.zig.parent / "lib/libc/include/generic-musl/elf.h",
                            build / "elf.h")
        cases = [
            ("module_symbols_test", [src / "module_symbols.cpp", src / "vermagic.cpp"]),
            ("handoff_test", []),
            ("log_test", []),
        ]
        for name, sources in cases:
            binary = build / (name + (".exe" if os.name == "nt" else ""))
            command = [*compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                       "-I" + str(build), "-I" + str(src), str(tests / (name + ".cpp")),
                       *map(str, sources), "-o", str(binary)]
            if args.sanitize:
                command.extend(["-fsanitize=address,undefined", "-fno-omit-frame-pointer"])
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True, timeout=60)
            print(f"PASS {name}", flush=True)


if __name__ == "__main__":
    main()
