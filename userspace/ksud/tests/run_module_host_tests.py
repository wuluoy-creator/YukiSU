"""Run module-management regressions without Android, root, or live modules.

Linux: python3 userspace/ksud/tests/run_module_host_tests.py
Windows: python userspace/ksud/tests/run_module_host_tests.py --zig /path/to/zig.exe
"""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def extract_function(source, signature):
    start = source.index(signature)
    # Production functions have their closing brace at column zero; nested
    # scopes are indented. Keep their bodies verbatim for fault injection.
    end = source.index("\n}", start) + 2
    return source[start:end] + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    parser.add_argument("--zig", help="Use Zig's bundled C++ compiler")
    args = parser.parse_args()
    compiler = [args.zig, "c++"] if args.zig else [args.cxx]
    tests = Path(__file__).resolve().parent
    module = tests.parent / "src/module"
    groups = [
        (module / "module_utils.hpp", [
            "inline bool is_module_directory(", "inline bool validate_module_id("]),
        (module / "module.cpp", [
            "bool exec_install_script(", "int module_uninstall_impl(",
            "int module_uninstall(", "int module_disable_impl(", "int module_disable(",
            "int uninstall_all_modules(", "int disable_all_modules("]),
        (module / "module_config.cpp", [
            "std::string get_module_id(", "std::string get_config_dir(",
            "std::map<std::string, std::string> load_config(", "bool save_config(",
            "int module_config_handle("]),
    ]
    with tempfile.TemporaryDirectory(prefix="module-host-tests-") as temporary:
        build = Path(temporary)
        extracted = []
        for path, signatures in groups:
            source = path.read_text(encoding="utf-8")
            extracted.extend(extract_function(source, signature) for signature in signatures)
        (build / "module_under_test.inc").write_text("\n".join(extracted), encoding="utf-8")
        binary = build / ("module_management_test.exe" if os.name == "nt"
                          else "module_management_test")
        subprocess.run([*compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(build), str(tests / "module_management_test.cpp"),
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
