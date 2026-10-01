import base64
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import manager_certificate

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from repack_apk import verify_manager_certificate


class CertificateIdentityTests(unittest.TestCase):
    def test_kernel_certificate_size_limits(self):
        for size in (1, 1024):
            with self.subTest(size=size):
                certificate = bytes(range(256)) * (size // 256) + b"x" * (size % 256)
                self.assertEqual(
                    manager_certificate.certificate_identity(certificate),
                    (size, hashlib.sha256(certificate).hexdigest()),
                )
        for certificate in (b"", b"x" * 1025):
            with self.subTest(size=len(certificate)), self.assertRaises(ValueError):
                manager_certificate.certificate_identity(certificate)

    def test_missing_credentials_do_not_run_keytool(self):
        complete = {
            "MANAGER_KEYSTORE": base64.b64encode(b"test-keystore").decode(),
            "KEY_ALIAS": "test-alias",
            "KEYSTORE_PASSWORD": "test-password",
        }
        for name in complete:
            with self.subTest(name=name), patch.object(subprocess, "run") as run:
                environ = dict(complete)
                del environ[name]
                with self.assertRaises(ValueError):
                    manager_certificate.export_certificate(environ)
                run.assert_not_called()

    def test_invalid_base64_does_not_run_keytool(self):
        with patch.object(subprocess, "run") as run:
            with self.assertRaises(ValueError):
                manager_certificate.export_certificate({
                    "MANAGER_KEYSTORE": "not-valid-base64!",
                    "KEY_ALIAS": "test-alias",
                    "KEYSTORE_PASSWORD": "test-password",
                })
            run.assert_not_called()

    def test_keytool_error_hides_credentials_and_cleans_up_keystore(self):
        environ = {
            "MANAGER_KEYSTORE": base64.b64encode(b"test-keystore").decode(),
            "KEY_ALIAS": "test-alias",
            "KEYSTORE_PASSWORD": "private-test-password",
        }
        exported_paths = []

        def failed_keytool(command, **kwargs):
            keystore_path = Path(command[command.index("-keystore") + 1])
            exported_paths.append(keystore_path)
            self.assertEqual(keystore_path.read_bytes(), b"test-keystore")
            self.assertNotIn(environ["KEYSTORE_PASSWORD"], command)
            return subprocess.CompletedProcess(command, 1, b"", b"private-test-password")

        with patch.object(subprocess, "run", side_effect=failed_keytool):
            with self.assertRaises(ValueError) as error:
                manager_certificate.export_certificate(environ)
        self.assertNotIn(environ["KEYSTORE_PASSWORD"], str(error.exception))
        self.assertEqual(len(exported_paths), 1)
        self.assertFalse(exported_paths[0].parent.exists())


@unittest.skipUnless(shutil.which("keytool"), "Java keytool is required for certificate export integration")
class KeytoolCertificateTests(unittest.TestCase):
    def test_export_matches_real_keystore_certificate(self):
        with tempfile.TemporaryDirectory(prefix="manager-signing-test-") as directory:
            keystore = Path(directory) / "test.jks"
            environ = dict(os.environ, KEYSTORE_PASSWORD="test-store-password")
            subprocess.run(
                [
                    "keytool", "-genkeypair", "-alias", "test-manager", "-keyalg", "RSA",
                    "-keysize", "2048", "-validity", "1", "-storetype", "JKS",
                    "-dname", "CN=Manager Certificate Test", "-keystore", str(keystore),
                    "-storepass:env", "KEYSTORE_PASSWORD", "-keypass:env", "KEYSTORE_PASSWORD",
                ],
                env=environ, capture_output=True, check=True,
            )
            expected = subprocess.run(
                [
                    "keytool", "-exportcert", "-keystore", str(keystore),
                    "-alias", "test-manager", "-storepass:env", "KEYSTORE_PASSWORD",
                ],
                env=environ, capture_output=True, check=True,
            ).stdout
            environ["MANAGER_KEYSTORE"] = base64.encodebytes(keystore.read_bytes()).decode()
            environ["KEY_ALIAS"] = "test-manager"
            certificate = manager_certificate.export_certificate(environ)
            self.assertEqual(certificate, expected)
            size, digest = manager_certificate.certificate_identity(certificate)
            self.assertLessEqual(size, 1024)
            self.assertEqual(digest, hashlib.sha256(expected).hexdigest())

            environ["KEYSTORE_PASSWORD"] = "wrong-test-password"
            with self.assertRaises(ValueError):
                manager_certificate.export_certificate(environ)


class FinalApkCertificateTests(unittest.TestCase):
    digest = "ab" * 32

    def verify(self, output, returncode=0):
        with patch.object(
            subprocess, "run", return_value=subprocess.CompletedProcess([], returncode, output)
        ):
            verify_manager_certificate(Path("apksigner"), Path("manager.apk"), self.digest)

    def test_matching_verified_certificate_is_accepted(self):
        self.verify(
            "Verifies\nSigner #1 certificate DN: CN=Manager\n"
            f"Signer #1 certificate SHA-256 digest: {self.digest.upper()}\n"
        )

    def test_other_signing_key_is_rejected(self):
        with self.assertRaises(RuntimeError):
            self.verify(f"Signer #1 certificate SHA-256 digest: {'cd' * 32}\n")

    def test_failed_apk_verification_rejects_matching_certificate(self):
        with self.assertRaises(RuntimeError):
            self.verify(f"Signer #1 certificate SHA-256 digest: {self.digest}\n", returncode=1)

    def test_missing_or_multiple_signers_are_rejected(self):
        outputs = (
            "Verifies\n",
            f"Signer #1 certificate SHA-256 digest: {self.digest}\n"
            f"Signer #2 certificate SHA-256 digest: {self.digest}\n",
        )
        for output in outputs:
            with self.subTest(output=output), self.assertRaises(RuntimeError):
                self.verify(output)

    def test_invalid_expected_digest_is_rejected_before_running_apksigner(self):
        for digest in ("", self.digest.upper(), "ab" * 31, "zz" * 32):
            with self.subTest(digest=digest), patch.object(subprocess, "run") as run:
                with self.assertRaises(ValueError):
                    verify_manager_certificate(Path("apksigner"), Path("manager.apk"), digest)
                run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
