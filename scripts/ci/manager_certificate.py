"""Export the configured Manager's public certificate for kernel builds."""

import base64
import hashlib
import os
import subprocess
import tempfile
from pathlib import Path


def certificate_identity(certificate: bytes) -> tuple[int, str]:
    # Keep this limit in sync with CERT_MAX_LENGTH in kernel/manager/apk_sign.c.
    if not 0 < len(certificate) <= 1024:
        raise ValueError("Manager certificate must contain 1-1024 bytes for kernel verification")
    return len(certificate), hashlib.sha256(certificate).hexdigest()


def export_certificate(environ: dict[str, str]) -> bytes:
    required = ("MANAGER_KEYSTORE", "KEY_ALIAS", "KEYSTORE_PASSWORD")
    if any(not environ.get(name) for name in required):
        raise ValueError("Manager certificate export requires KEYSTORE, KEY_ALIAS and KEYSTORE_PASSWORD")
    keystore = base64.b64decode("".join(environ["MANAGER_KEYSTORE"].split()), validate=True)
    with tempfile.TemporaryDirectory(prefix="manager-certificate-") as directory:
        keystore_path = Path(directory) / "manager.jks"
        with keystore_path.open("xb") as stream:
            keystore_path.chmod(0o600)
            stream.write(keystore)
        result = subprocess.run(
            [
                "keytool", "-exportcert", "-keystore", str(keystore_path),
                "-alias", environ["KEY_ALIAS"], "-storepass:env", "KEYSTORE_PASSWORD",
            ],
            env=environ, capture_output=True, check=False,
        )
        if result.returncode != 0:
            # Do not echo keytool output or signing credentials into CI logs.
            raise ValueError("Cannot export Manager certificate; check the keystore, alias and password")
        return result.stdout


def main() -> None:
    size, digest = certificate_identity(export_certificate(dict(os.environ)))
    with Path(os.environ["GITHUB_OUTPUT"]).open("a", encoding="utf-8") as output:
        output.write(f"size={size}\nsha256={digest}\n")
    print(f"Kernel will trust Manager certificate: {size} bytes, SHA-256 {digest}")


if __name__ == "__main__":
    main()
