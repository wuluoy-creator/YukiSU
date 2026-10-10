import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from prepare_release import package_release
from resolve_versions import main, release_repository, resolve, version_code, version_name


class ReleaseVersionTests(unittest.TestCase):
    def resolve(self, inputs=None, **kwargs):
        return resolve(inputs or {}, describe="v1.7.0-84-ga5671f7e", commit_count=3616, **kwargs)

    def test_release_repository_validates_current_repository(self):
        self.assertEqual(release_repository("wuluoy-creator/ZySU"), "wuluoy-creator/ZySU")
        for invalid in ("", "https://github.com/owner/repo", "owner/repo/extra", "owner/repo\n"):
            with self.subTest(repository=invalid), self.assertRaises(ValueError):
                release_repository(invalid)

    def test_version_resolution_uses_current_repository_and_migrated_release_anchor(self):
        release = {"tag_name": "v1.8.0", "assets": [{"name": "ZySU_v1.8.0_20000-release.apk"}]}
        git_results = {
            ("rev-list", "--count", "refs/tags/v1.8.0", "--"): "3616",
            ("describe", "--tags", "--always", "--abbrev=8"): "v1.8.0-3-ga5671f7e",
            ("rev-list", "--count", "HEAD"): "3619",
            ("rev-parse", "HEAD"): "a5671f7e" * 5,
        }
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "output"
            summary = Path(tmp) / "summary"
            env = {
                "GITHUB_REPOSITORY": "wuluoy-creator/ZySU",
                # Old Actions configuration must never redirect version checks.
                "RELEASE_REPOSITORY": "legacy/downloads",
                "GH_TOKEN": "current-repository-token",
                "GITHUB_OUTPUT": str(output),
                "GITHUB_STEP_SUMMARY": str(summary),
            }
            with patch.dict(os.environ, env, clear=True), \
                 patch("resolve_versions.list_releases", return_value=[release]) as fetch, \
                 patch("resolve_versions.git", side_effect=lambda *args: git_results[args]):
                main()
            fetch.assert_called_once_with("wuluoy-creator/ZySU", "current-repository-token")
            self.assertIn("manager_version_code=20003\n", output.read_text(encoding="utf-8"))
            self.assertIn("Release repository: `wuluoy-creator/ZySU`", summary.read_text(encoding="utf-8"))

    def test_existing_git_version_and_code(self):
        versions = self.resolve()
        self.assertEqual(versions["manager_version_name"], "v1.7.0-a5671f7e")
        self.assertEqual(versions["ksud_version_name"], "1.7.0-a5671f7e")
        self.assertEqual(versions["manager_version_code"], 10481)

    def test_manual_names_are_independent_and_tag_defaults(self):
        versions = self.resolve({"publish_release": True, "release_tag": "v1.8.0",
                                 "ksud_version_name": "1.8.1", "manager_version_code": "20000"})
        self.assertEqual(versions["manager_version_name"], "v1.8.0")
        self.assertEqual(versions["ksud_version_name"], "1.8.1")
        self.assertEqual(versions["kernel_version_name"], "1.8.0")
        self.assertEqual(versions["manager_version_code"], 20000)

    def test_automatic_code_exceeds_published_custom_code_including_prerelease(self):
        releases = [{"tag_name": "v1.8.0", "prerelease": True, "assets": [{"name": "ZySU_v1.8.0_20000-release.apk"}]}]
        anchors = {"v1.8.0": 3616}
        self.assertEqual(self.resolve(releases=releases, release_commit_counts=anchors)["manager_version_code"], 20000)
        with self.assertRaisesRegex(ValueError, "greater than"):
            self.resolve({"publish_release": True, "release_tag": "v1.9.0", "manager_version_code": "20000"}, releases=releases, release_commit_counts=anchors)
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.resolve({"publish_release": True, "release_tag": "v1.8.0"}, releases=releases)

    def test_custom_release_preserves_incrementing_ci_versions_and_release_upgrade(self):
        releases = [{"tag_name": "v1.8.0", "assets": [{"name": "ZySU_v1.8.0_20000-release.apk"}]}]
        for count in (3616, 3617, 3618, 3619):
            kwargs = dict(describe="v1.8.0-3-ga5671f7e", commit_count=count,
                          releases=releases, release_commit_counts={"v1.8.0": 3616})
            ci = resolve({}, **kwargs)
            release = resolve({"publish_release": True, "release_tag": "v1.8.1"}, **kwargs)
            self.assertEqual(ci["manager_version_code"], 20000 + count - 3616)
            self.assertEqual(release["manager_version_code"], ci["manager_version_code"] + 1)
        with self.assertRaisesRegex(ValueError, "anchor"):
            self.resolve(releases=releases)
        with self.assertRaisesRegex(ValueError, "upgrade builds"):
            resolve({"publish_release": True, "release_tag": "v1.9.0", "manager_version_code": "20001"},
                    describe="v1.8.0-3-ga5671f7e", commit_count=3619, releases=releases,
                    release_commit_counts={"v1.8.0": 3616})

    def test_tag_push_and_build_without_publishing(self):
        self.assertEqual(self.resolve(ref_type="tag", ref_name="v1.8.0")["manager_version_name"], "v1.8.0")
        self.assertFalse(self.resolve({"release_tag": "v1.8.0"})["publish_release"])
        with self.assertRaisesRegex(ValueError, "tag is required"):
            self.resolve({"publish_release": True})

    def test_reject_unsafe_versions_and_invalid_codes(self):
        for name in ("v1.8.0\nBAD=x", " 1.8.0", "$(id)", '1.8.0"', "1.8", "1.8.0-" + "a" * 81):
            with self.subTest(name=name), self.assertRaises(ValueError):
                version_name(name, "test")
        for code in ("0", "-1", "1.5", True, "2100000001", "12\nX=y"):
            with self.subTest(code=code), self.assertRaises(ValueError):
                version_code(code, "test")

    def test_package_legacy_filename_and_metadata_describe_actual_apk(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            manager = root / "artifacts/Manager-arm64-v8a"
            manager.mkdir(parents=True)
            (manager / "app-release.apk").write_bytes(b"signed APK fixture")
            (manager / "app-release.sig").write_bytes(b"signature")
            ksud = root / "artifacts/ksud-aarch64-linux-android"
            ksud.mkdir()
            (ksud / "ksud").write_bytes(b"daemon without optional GPG signature")
            metadata = package_release(root / "artifacts", root / "release", "v1.8.0", "20001",
                                       ksud_name="1.8.1", ksud_code="20001", kernel_name="1.8.0", kernel_code="20001")
            name = "ZySU_v1.8.0_20001-release.apk"
            self.assertEqual(metadata["apk"]["name"], name)
            self.assertEqual(metadata["apk"]["size"], 18)
            self.assertEqual(len(metadata["apk"]["sha256"]), 64)
            self.assertTrue((root / "release" / (name + ".sig")).is_file())
            self.assertTrue((root / "release/ksud-aarch64-linux-android").is_file())
            self.assertEqual(json.loads((root / "release/update.json").read_text()), metadata)


if __name__ == "__main__":
    unittest.main()
