"""Run SUMHP daemon transport regressions with real POSIX sockets.

Linux: python3 userspace/ksud/tests/run_sumhp_daemon_host_tests.py
Windows fault injection: add --zig /path/to/zig.exe --portable-only
Windows cross-check: add --zig /path/to/zig.exe --target x86_64-linux-musl --compile-only
The tests do not start the production daemon or require Android or root.
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
    parser.add_argument("--portable-only", action="store_true")
    args = parser.parse_args()
    if args.target and not args.zig:
        parser.error("--target requires --zig")
    compiler = [args.zig, "c++"] if args.zig else [args.cxx]
    if args.target:
        compiler += ["-target", args.target]
    ksud = Path(__file__).resolve().parents[1]
    source = (ksud / "sumhp/src/core/daemon.cpp").read_text(encoding="utf-8")
    # Exercise the production framing, socket I/O and retry decision directly.
    # Android mount restoration and background process setup are unrelated here.
    sections = []
    for start, stop in [
        ("std::string join_request(", "\nstd::string status_json("),
        ("using Deadline =", "\nbool daemon_running()"),
        ("int run_via_daemon(", "\nnamespace {\nint print_status_json()"),
    ]:
        begin = source.index(start)
        sections.append(source[begin:source.index(stop, begin)])
    with tempfile.TemporaryDirectory(prefix="sumhp-daemon-tests-") as temporary:
        build = Path(temporary)
        begin = source.index("using Deadline =")
        (build / "sumhp_daemon_io_under_test.inc").write_text(
            source[begin:source.index("\nvoid send_response(", begin)], encoding="utf-8")
        portable = build / ("sumhp_daemon_io_test.exe" if os.name == "nt" and not args.target
                            else "sumhp_daemon_io_test")
        subprocess.run([
            *compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread",
            "-I" + str(build), str(ksud / "tests/sumhp_daemon_io_test.cpp"),
            "-o", str(portable),
        ], check=True)
        if not args.compile_only:
            subprocess.run([str(portable)], check=True, timeout=30)
        if args.portable_only:
            return
        (build / "sumhp_daemon_under_test.inc").write_text(
            "\n".join(sections), encoding="utf-8")
        binary = build / "sumhp_daemon_transport_test"
        subprocess.run([
            *compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread",
            "-I" + str(build), "-I" + str(ksud / "sumhp/src"),
            "-I" + str(ksud / "src"),
            str(ksud / "tests/sumhp_daemon_transport_test.cpp"), "-o", str(binary),
        ], check=True)
        if args.compile_only:
            print("SUMHP daemon regression binary compiled; execution skipped as requested.")
        else:
            subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    main()
