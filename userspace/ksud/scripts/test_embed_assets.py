#!/usr/bin/env python3

import hashlib
import lzma
import re
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path


SCRIPT = Path(__file__).with_name("embed_assets.py")


def extract_array(source: str, name: str) -> bytes:
    match = re.search(
        rf"static const unsigned char {re.escape(name)}\[\] = \{{(.*?)\n\}};",
        source,
        re.DOTALL,
    )
    if not match:
        raise AssertionError(f"array not found: {name}")
    return bytes(int(value, 16) for value in re.findall(r"0x([0-9a-f]{2})", match.group(1)))


class EmbedAssetsTest(unittest.TestCase):
    def test_unsupported_lkms_are_rejected(self) -> None:
        for kmi in ("android12-5.10", "android14-5.15", "6.0", "unknown"):
            with self.subTest(kmi=kmi), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                assets = root / "assets"
                assets.mkdir()
                (assets / f"{kmi}_kernelsu.ko").write_bytes(b"old module")
                generated = root / "assets_data.cpp"
                result = subprocess.run(
                    [sys.executable, str(SCRIPT), str(assets), str(generated)],
                    capture_output=True,
                    text=True,
                )
                self.assertNotEqual(0, result.returncode)
                self.assertIn("requires Linux 6.1 or newer", result.stderr)
                self.assertFalse(generated.exists())

    def test_assets_without_lkms_remain_zlib_only(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            assets = root / "assets"
            assets.mkdir()
            payload = b"ordinary asset" * 128
            (assets / "payload.bin").write_bytes(payload)
            generated = root / "assets_data.cpp"

            subprocess.run(
                [sys.executable, str(SCRIPT), str(assets), str(generated)],
                check=True,
                capture_output=True,
                text=True,
            )
            source = generated.read_text(encoding="utf-8")
            self.assertNotIn("asset_lkm_pack_xz", source)
            self.assertEqual(
                payload,
                zlib.decompress(extract_array(source, "asset_payload_bin")),
            )

    def test_lkms_share_one_deterministic_xz_stream(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            assets = root / "assets"
            assets.mkdir()
            common = b"\x7fELF" + b"\0" * 4096 + (b"shared-kernel-symbol\0" * 1024)
            first = common + b"android14" * 512
            second = common + b"android16" * 512
            (assets / "android16-6.12_kernelsu.ko").write_bytes(second)
            (assets / "android14-6.1_kernelsu.ko").write_bytes(first)
            (assets / "installer.sh").write_bytes(b"#!/bin/sh\r\necho ok\r\n")

            generated = root / "assets_data.cpp"
            subprocess.run(
                [sys.executable, str(SCRIPT), str(assets), str(generated)],
                check=True,
                capture_output=True,
                text=True,
            )
            source = generated.read_text(encoding="utf-8")
            first_output = generated.read_bytes()
            subprocess.run(
                [sys.executable, str(SCRIPT), str(assets), str(generated)],
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertEqual(first_output, generated.read_bytes())

            packed = extract_array(source, "asset_lkm_pack_xz")
            self.assertEqual(1, packed[7])
            self.assertEqual(first + second, lzma.decompress(packed))
            self.assertLess(
                len(packed),
                len(lzma.compress(first, preset=6)) + len(lzma.compress(second, preset=6)),
            )
            self.assertIn("AssetCodec::SolidXz, 0,", source)
            self.assertIn(f"AssetCodec::SolidXz, {len(first)},", source)
            self.assertEqual(
                hashlib.sha256(first).digest(),
                extract_array(source, "asset_android14_6_1_kernelsu_ko_sha256"),
            )
            self.assertEqual(
                hashlib.sha256(second).digest(),
                extract_array(source, "asset_android16_6_12_kernelsu_ko_sha256"),
            )

            script = extract_array(source, "asset_installer_sh")
            self.assertEqual(b"#!/bin/sh\necho ok\n", zlib.decompress(script))

            corrupted = bytearray(packed)
            corrupted[len(corrupted) // 2] ^= 0x80
            with self.assertRaises(lzma.LZMAError):
                lzma.decompress(corrupted)


if __name__ == "__main__":
    unittest.main()
