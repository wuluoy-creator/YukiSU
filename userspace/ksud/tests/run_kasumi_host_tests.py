"""Run Kasumi client, restoration and persistence regressions with a host C++17 compiler.

Linux: python3 userspace/ksud/tests/run_kasumi_host_tests.py
Windows: python userspace/ksud/tests/run_kasumi_host_tests.py --zig /path/to/zig.exe
The tests use a fake ioctl and do not require a running Kasumi kernel or root.
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
    args = parser.parse_args()
    compiler = [args.zig, "c++"] if args.zig else [args.cxx]
    root = Path(__file__).resolve().parents[3]
    tests = root / "userspace/ksud/tests"
    kagami = root / "userspace/ksud/kagami"

    with tempfile.TemporaryDirectory(prefix="kasumi-host-tests-") as temporary:
        build = Path(temporary)
        flags = ["-std=c++17", "-Wall", "-Wextra", "-Werror",
                 "-I" + str(root), "-I" + str(kagami / "include"),
                 "-I" + str(kagami / "src"), "-I" + str(build)]
        if os.name == "nt":
            # Only the fake transport uses these headers. Production UAPI and
            # Android builds continue to use their real Linux definitions.
            (build / "linux").mkdir()
            (build / "sys").mkdir()
            (build / "linux/types.h").write_text(
                "#pragma once\n#include <stdint.h>\n"
                "typedef uint32_t __u32;\ntypedef int32_t __s32;\n"
                "#define __aligned_u64 uint64_t __attribute__((aligned(8)))\n")
            (build / "sys/ioctl.h").write_text(
                "#pragma once\n"
                "#define _IOC(d,t,n,s) (((unsigned long)(d)<<30)|((unsigned long)(s)<<16)|"
                "((unsigned long)(t)<<8)|(n))\n"
                "#define _IO(t,n) _IOC(0,t,n,0)\n"
                "#define _IOR(t,n,s) _IOC(2,t,n,sizeof(s))\n"
                "#define _IOW(t,n,s) _IOC(1,t,n,sizeof(s))\n"
                "#define _IOWR(t,n,s) _IOC(3,t,n,sizeof(s))\n")

        source = (kagami / "src/mount/kasumi.cpp").read_text()
        begin = source.index("bool restore_user_hide_rules(std::string& error, bool retry) {")
        end = source.index("\n}  // namespace", begin)
        (build / "kasumi_restore_under_test.inc").write_text(source[begin:end])
        source = (kagami / "src/core/config.cpp").read_text()
        begin = source.index("bool load_user_hide_rules(std::vector<std::string>& rules,")
        end = source.index("\nstd::vector<std::string> load_user_hide_rules()", begin)
        (build / "kasumi_persistence_under_test.inc").write_text(source[begin:end])

        for name, sources in [
            ("kasumi_client_test", [tests / "kasumi_client_test.cpp",
                                    kagami / "src/mount/kasumi_client.cpp"]),
            ("kasumi_restore_test", [tests / "kasumi_restore_test.cpp"]),
            ("kasumi_persistence_test", [tests / "kasumi_persistence_test.cpp"]),
        ]:
            binary = build / (name + (".exe" if os.name == "nt" else ""))
            subprocess.run([*compiler, *flags, *map(str, sources), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
