"""Run portable ksud regressions without root or an Android device.

Linux: python3 userspace/ksud/tests/run_host_tests.py --cxx clang++
Windows: python userspace/ksud/tests/run_host_tests.py --zig /path/to/zig.exe
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
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    compiler = [str(args.zig), "c++"] if args.zig else [args.cxx]
    tests = Path(__file__).resolve().parent
    src = tests.parent / "src"

    with tempfile.TemporaryDirectory(prefix="ksud-host-tests-") as temporary:
        build = Path(temporary)
        if os.name == "nt":
            if not args.zig:
                parser.error("Windows tests require --zig for the ELF header")
            # Only elf.h is needed; keep Linux libc headers off the host include path.
            shutil.copyfile(args.zig.parent / "lib/libc/include/generic-musl/elf.h",
                            build / "elf.h")
        source = (src / "boot/tools.cpp").read_text(encoding="utf-8")
        begin = source.index("bool exec_dd(")
        end = source.index("\n// `blockdev", begin)
        (build / "boot_copy_under_test.inc").write_text(source[begin:end], encoding="utf-8")
        cases = [
            ("elf_symbols_test", []),
            ("boot_copy_test", []),
            ("kernel_version_test", []),
            ("su_args_test", [src / "su_args.cpp"]),
            ("xperm_parser_test", [src / "sepolicy/xperm_parser.cpp"]),
            ("boot_image_btf_test", [src / "boot/boot_image_btf.cpp"]),
        ]
        for name, sources in cases:
            binary = build / (name + (".exe" if os.name == "nt" else ""))
            extra_flags = (["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
                           if args.sanitize else [])
            command = [*compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                       *extra_flags,
                       "-I" + str(build), "-I" + str(src), str(tests / (name + ".cpp")),
                       *map(str, sources), "-o", str(binary)]
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True, timeout=60)
            print(f"PASS {name}", flush=True)


if __name__ == "__main__":
    main()
