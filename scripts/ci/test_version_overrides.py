"""Focused version-generation tests; no Android or kernel toolchain required."""

import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ksud = load_module("ksud_version", ROOT / "userspace/ksud/scripts/generate_version.py")
kernel = load_module("kernel_version", ROOT / "kernel/tools/version_overrides.py")


class KsudVersionTests(unittest.TestCase):
    def test_empty_overrides_preserve_git_version(self):
        with patch.dict(os.environ, {"ZYSU_KSUD_VERSION_NAME": "", "ZYSU_KSUD_VERSION_CODE": ""}), \
             patch.object(ksud, "has_ksud_nc", return_value=False), \
             patch.object(ksud.subprocess, "check_output", side_effect=["3616\n", "v1.8.0-4-g1234abcd\n"]):
            self.assertEqual(ksud.get_git_version(), (10481, "1.8.0-1234abcd"))

    def test_overrides_do_not_require_git_and_dirty_suffix_is_kept(self):
        for name, dirty, expected in (("v1.8.1-rc.1", False, "1.8.1-rc.1"),
                                      ("v1.8.1-rc.1", True, "1.8.1-rc.1-nc"),
                                      ("1.8.1-nc", True, "1.8.1-nc")):
            with self.subTest(name=name, dirty=dirty), \
                 patch.dict(os.environ, {"ZYSU_KSUD_VERSION_NAME": name, "ZYSU_KSUD_VERSION_CODE": "12042"}), \
                 patch.object(ksud, "has_ksud_nc", return_value=dirty), \
                 patch.object(ksud.subprocess, "check_output") as git:
                self.assertEqual(ksud.get_git_version(), (12042, expected))
                git.assert_not_called()

    def test_overrides_can_be_set_independently(self):
        cases = [
            ({"ZYSU_KSUD_VERSION_NAME": "v2.0.0", "ZYSU_KSUD_VERSION_CODE": ""}, "4000", (10865, "2.0.0")),
            ({"ZYSU_KSUD_VERSION_NAME": "", "ZYSU_KSUD_VERSION_CODE": "12000"}, "v1.8.0", (12000, "1.8.0")),
        ]
        for environ, git_result, expected in cases:
            with self.subTest(environ=environ), patch.dict(os.environ, environ), \
                 patch.object(ksud, "has_ksud_nc", return_value=False), \
                 patch.object(ksud.subprocess, "check_output", return_value=git_result):
                self.assertEqual(ksud.get_git_version(), expected)

    def test_prerelease_git_describe_matches_manager_normalization(self):
        self.assertEqual(ksud.normalize_version_name("v1.8.0-rc.1-4-g1234abcd\n"), "1.8.0-rc.1-1234abcd")

    def test_invalid_override_fails_before_git_fallback(self):
        for variable, values in (
            ("ZYSU_KSUD_VERSION_NAME", (" 1.8.0", "1.8.0\n", "1.8.0\";exit", "1.8.0-" + "a" * 80)),
            ("ZYSU_KSUD_VERSION_CODE", ("0", "-1", "2100000001", " 42", "42\n", "1.0", "9" * 10000)),
        ):
            for value in values:
                with self.subTest(variable=variable, value=value[:40]), \
                     patch.dict(os.environ, {"ZYSU_KSUD_VERSION_NAME": "", "ZYSU_KSUD_VERSION_CODE": "", variable: value}), \
                     patch.object(ksud, "has_ksud_nc") as dirty, \
                     self.assertRaises(ValueError):
                    ksud.get_git_version()
                dirty.assert_not_called()

    def test_dirty_bundled_kernel_prerelease_is_recognized(self):
        for name in (b"v1.8.0-rc.1-1234abcd-nc@ZySU", b"1.8.0+custom.2-nc", b"1234abcd-nc"):
            with self.subTest(name=name):
                self.assertIsNotNone(ksud.DIRTY_VERSION_PATTERN.search(b"\x00" + name + b"\x00"))
        self.assertIsNone(ksud.DIRTY_VERSION_PATTERN.search(b"v1.8.0-rc.1-1234abcd@ZySU\x00"))

    def test_cli_generates_real_cpp_values_and_rejects_invalid_input(self):
        with tempfile.TemporaryDirectory(prefix="zysu-version-") as directory:
            output = Path(directory) / "defs.cpp"
            env = dict(os.environ, ZYSU_KSUD_VERSION_NAME="v2.3.4", ZYSU_KSUD_VERSION_CODE="12042")
            result = subprocess.run([sys.executable, ksud.__file__, str(output)], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            content = output.read_text()
            self.assertIn('VERSION_CODE = "12042"', content)
            self.assertRegex(content, r'VERSION_NAME = "2\.3\.4(?:-nc)?"')
            env["ZYSU_KSUD_VERSION_NAME"] = "2.3.4\n"
            result = subprocess.run([sys.executable, ksud.__file__, str(output)], env=env, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_text(), content)


class KernelOverrideTests(unittest.TestCase):
    def test_names_and_code_boundaries(self):
        self.assertEqual(kernel.get_overrides({}), ("-", "-"))
        self.assertEqual(kernel.get_overrides({"ZYSU_KERNEL_VERSION_NAME": "", "ZYSU_KERNEL_VERSION_CODE": ""}), ("-", "-"))
        for code in ("1", "2100000000"):
            self.assertEqual(kernel.get_overrides({"ZYSU_KERNEL_VERSION_NAME": "v1.8.0-rc.1", "ZYSU_KERNEL_VERSION_CODE": code}), ("1.8.0-rc.1", code))

    def test_injection_whitespace_and_out_of_range_values_are_rejected(self):
        for variable, values in (
            ("ZYSU_KERNEL_VERSION_NAME", ("1.8.0\n", " 1.8.0", "1.8.0 ", "1.8.0;echo injected", "$(shell echo injected)", "1.8.0\"", "1.8.0-" + "x" * 80)),
            ("ZYSU_KERNEL_VERSION_CODE", ("0", "-1", "2100000001", "42\n", " 42", "$(shell echo injected)", "9" * 10000)),
        ):
            for value in values:
                with self.subTest(variable=variable, value=value[:40]), self.assertRaises(ValueError):
                    kernel.get_overrides({variable: value})


@unittest.skipUnless(os.name == "posix" and shutil.which("make"), "GNU make integration requires a POSIX host")
class KernelKbuildTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="zysu-kbuild-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.makefile = self.root / "Makefile"
        self.makefile.write_text(
            f"src := {ROOT / 'kernel'}\nsrctree := {self.root}\n"
            f"include {ROOT / 'kernel/Kbuild'}\n"
            "all:\n\t@printf '%s\\n' '$(KSU_VERSION)' '$(KSU_VERSION_FULL)' '$(ccflags-y)'\n"
        )
        self.env = dict(os.environ, ZYSU_KERNEL_VERSION_NAME="v2.3.4-rc.1", ZYSU_KERNEL_VERSION_CODE="12042")
        # A custom name and code must not require a release API call or fetch.
        (self.root / "curl").write_text("#!/bin/sh\ntouch \"$ZYSU_NETWORK_MARKER\"\nexit 99\n")
        (self.root / "curl").chmod(0o755)
        self.env["ZYSU_NETWORK_MARKER"] = str(self.root / "network-used")
        self.env["PATH"] = str(self.root) + os.pathsep + self.env["PATH"]

    def run_make(self):
        return subprocess.run(["make", "--no-print-directory", "-f", str(self.makefile), "all"], env=self.env, capture_output=True, text=True)

    def test_custom_version_reaches_compiler_flags_without_network(self):
        result = self.run_make()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("-DKSU_VERSION=12042", result.stdout)
        self.assertRegex(result.stdout, r"v2\.3\.4-rc\.1-[0-9a-f]{8}(?:-nc)?@ZySU")
        self.assertFalse((self.root / "network-used").exists())

    def test_make_expression_is_not_executed_before_validation(self):
        marker = self.root / "injected"
        self.env["ZYSU_KERNEL_VERSION_NAME"] = f"$(shell touch {marker})"
        result = self.run_make()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(marker.exists())
        self.assertIn("Invalid kernel version override", result.stderr)


if __name__ == "__main__":
    unittest.main()
